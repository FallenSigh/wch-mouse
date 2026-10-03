/*
 * src/app/air_mouse.c - air-mouse mode: the platform side of the algorithm.
 *
 * The optical sensor and the IMU never both own the cursor, so entering the
 * mode powers the PAW3395 down and brings the BMI270 gyro + accelerometer up;
 * leaving it disables both sensors and re-runs the full optical bring-up. The
 * BMI270 API in this vendor tree has no suspend call, so "IMU suspend" is
 * bmi270_sensor_disable({BMI2_GYRO, BMI2_ACCEL}).
 *
 * The estimator and the pointer mapper live in the portable modules attitude.c
 * and pointer.c, which know nothing about this platform. This file is only the
 * port: it reads the IMU, converts to physical units, and feeds those modules;
 * it owns the combo detection, the sensor hand-off and the feedback.
 */

#include "air_mouse.h"

#include "attitude.h"
#include "pointer.h"

#include "bmi2.h"
#include "bmi2_defs.h"
#include "bmi270.h"
#include "log.h"
#include "motor.h"
#include "mouse.h"
#include "paw3395_port.h"

/* Side-1 (PR_SW4/BACK) + side-2 (PR_SW5/FWD) held this long enter/leave. */
#define AIR_MOUSE_HOLD_MS        2000u

/* Gyro samples averaged into the zero-bias at entry, after the start-up samples
 * below are discarded. At 200 Hz, 16 samples is 80 ms of "hold still"; motion
 * reporting starts once the bias is known. */
#define AIR_MOUSE_CALIB_SAMPLES  16u

/* Start-up samples dropped before averaging: the gyro needs ~30 ms after its
 * power mode is set before the output settles. */
#define AIR_MOUSE_CALIB_WARMUP   8u

/* Scaling for the configured BMI270 ranges: +-2000 dps -> 16.4 LSB/dps,
 * +-4 g -> 8192 LSB/g. The attitude module works in dps and g. */
#define AIR_GYRO_LSB_PER_DPS     16.4f
#define AIR_ACCEL_LSB_PER_G      8192.0f
#define AIR_GYRO_DT_S            0.005f

/* Pointer sensitivity presets: relative mouse counts per degree of rotation.
 * The stored index selects an (X, Y) pair; index 3 is the 36/32 default. */
static const uint8_t s_sens_x[] = { 9u, 18u, 27u, 36u, 54u, 72u, 108u };
static const uint8_t s_sens_y[] = { 8u, 16u, 24u, 32u, 48u, 64u,  96u };
#define AIR_SENS_COUNT ((uint8_t)(sizeof(s_sens_x) / sizeof(s_sens_x[0])))

/* IMU output-data-rate presets. The gyro reaches 3.2 kHz; the accelerometer
 * tops out at 1.6 kHz, so the last row pairs 3.2 kHz gyro with it. */
typedef struct {
    uint8_t gyr;
    uint8_t acc;
} air_odr_t;

static const air_odr_t s_odr[] = {
    { BMI2_GYR_ODR_25HZ,   BMI2_ACC_ODR_25HZ   },
    { BMI2_GYR_ODR_50HZ,   BMI2_ACC_ODR_50HZ   },
    { BMI2_GYR_ODR_100HZ,  BMI2_ACC_ODR_100HZ  },
    { BMI2_GYR_ODR_200HZ,  BMI2_ACC_ODR_200HZ  },
    { BMI2_GYR_ODR_400HZ,  BMI2_ACC_ODR_400HZ  },
    { BMI2_GYR_ODR_800HZ,  BMI2_ACC_ODR_800HZ  },
    { BMI2_GYR_ODR_1600HZ, BMI2_ACC_ODR_1600HZ },
    { BMI2_GYR_ODR_3200HZ, BMI2_ACC_ODR_1600HZ },
};
#define AIR_ODR_COUNT ((uint8_t)(sizeof(s_odr) / sizeof(s_odr[0])))

static air_mouse_cfg_t s_cfg;
static attitude_t      s_att;
static pointer_t       s_ptr;

static bool     s_active;
static bool     s_combo_held;
static bool     s_combo_armed;
static uint32_t s_combo_ms;

/* Active presets; the caller seeds these through air_mouse_cfg_t. */
static uint8_t s_sens_idx = 3u;
static uint8_t s_odr_idx  = 3u;

/* Latched by air_mouse_poll() for the read side. */
static uint8_t  s_raw_buttons;
static uint32_t s_now_ms;
static bool     s_was_ready;

/* Last BMI270 sensor-time sample (39.0625 us ticks), for a real integration
 * step instead of the nominal 5 ms. */
static uint32_t s_last_st;
static bool     s_have_st;

static void air_mouse_imu_on(void)
{
    struct bmi2_sens_config cfg[2] = { 0 };
    const uint8_t           sensors[2] = { BMI2_GYRO, BMI2_ACCEL };

    cfg[0].type                = BMI2_GYRO;
    cfg[0].cfg.gyr.odr         = s_odr[s_odr_idx].gyr;
    cfg[0].cfg.gyr.bwp         = BMI2_GYR_NORMAL_MODE;
    cfg[0].cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;
    cfg[0].cfg.gyr.noise_perf  = BMI2_POWER_OPT_MODE;
    cfg[0].cfg.gyr.range       = BMI2_GYR_RANGE_2000;
    cfg[0].cfg.gyr.ois_range   = BMI2_GYR_OIS_2000;

    cfg[1].type                = BMI2_ACCEL;
    cfg[1].cfg.acc.odr         = s_odr[s_odr_idx].acc;
    cfg[1].cfg.acc.bwp         = BMI2_ACC_NORMAL_AVG4;
    cfg[1].cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;
    cfg[1].cfg.acc.range       = BMI2_ACC_RANGE_4G;

    if (bmi2_set_sensor_config(cfg, 2, s_cfg.bmi) != BMI2_OK) {
        LOG_W("AIR", "gyro/accel config failed");
    }

    if (bmi270_sensor_enable(sensors, 2, s_cfg.bmi) != BMI2_OK) {
        LOG_W("AIR", "gyro/accel enable failed");
    }
}

/* Real integration step from the BMI270 sensor-time counter (24-bit, 39.0625
 * us ticks), falling back to the nominal period when the step is implausible. */
static float air_mouse_dt_from_sensor_time(uint32_t sens_time)
{
    const uint32_t st = sens_time & 0xFFFFFFu;
    float          dt = AIR_GYRO_DT_S;

    if (s_have_st) {
        const uint32_t ticks = (st - s_last_st) & 0xFFFFFFu;
        const float    cand  = (float)ticks * 0.0000390625f;

        if (cand >= 0.001f && cand <= 0.030f) {
            dt = cand;
        }
    }

    s_last_st = st;
    s_have_st = true;
    return dt;
}

uint8_t air_mouse_sens_count(void)
{
    return AIR_SENS_COUNT;
}

uint8_t air_mouse_odr_count(void)
{
    return AIR_ODR_COUNT;
}

void air_mouse_set_sensitivity(uint8_t idx)
{
    if (idx >= AIR_SENS_COUNT) {
        return;
    }

    s_sens_idx = idx;
    pointer_set_sensitivity(&s_ptr, (float)s_sens_x[idx], (float)s_sens_y[idx]);
}

void air_mouse_set_odr(uint8_t idx)
{
    if (idx >= AIR_ODR_COUNT) {
        return;
    }

    s_odr_idx = idx;

    if (s_active) {
        const uint8_t sensors[2] = { BMI2_GYRO, BMI2_ACCEL };

        /* Re-enable at the new rate. The bias and the attitude are kept; only
         * the sensor-time baseline restarts, because the cadence changed. */
        (void)bmi270_sensor_disable(sensors, 2, s_cfg.bmi);
        air_mouse_imu_on();
        s_have_st = false;
    }
}

static void air_mouse_enter(uint32_t now_ms)
{
    /* Fire the feedback first and do not wait for it: the hand-off below blocks
     * for tens of ms, so a blocking buzz would only start afterwards. */
    motor_pulse(MOTOR_MODE_SWITCH_MS);

    /* Hand the cursor over: optical sensor down, IMU up, then re-seed the
     * attitude and let it measure the zero-rate bias before any motion is
     * reported. The optical sensor's IRQ-driven read is parked first so its
     * blocking shutdown write cannot collide with an in-flight DMA burst. */
    paw3395_motion_stop();
    paw3395_shutdown(s_cfg.paw);
    air_mouse_imu_on();

    attitude_reset(&s_att);
    attitude_begin_calibration(&s_att, AIR_MOUSE_CALIB_WARMUP, AIR_MOUSE_CALIB_SAMPLES);

    pointer_reset(&s_ptr);
    pointer_set_sensitivity(&s_ptr, (float)s_sens_x[s_sens_idx], (float)s_sens_y[s_sens_idx]);

    s_have_st   = false;
    s_was_ready = false;
    s_active    = true;

    LOG_I("AIR", "enter: paw off, imu on, calibrating %u samples",
          (unsigned)AIR_MOUSE_CALIB_SAMPLES);
}

static void air_mouse_exit(uint32_t now_ms)
{
    const uint8_t sensors[2] = { BMI2_GYRO, BMI2_ACCEL };

    /* Same again: the optical bring-up below blocks for tens of ms. */
    motor_pulse(MOTOR_MODE_SWITCH_MS);

    /* IMU suspended: nothing reads it until the next entry. */
    if (bmi270_sensor_disable(sensors, 2, s_cfg.bmi) != BMI2_OK) {
        LOG_W("AIR", "gyro/accel disable failed");
    }

    /* The optical sensor comes back through the full bring-up, which resets
     * the driver's geometry, so re-apply the baseline the caller gave us. */
    paw3395_init(s_cfg.paw);
    paw3395_set_cpi(s_cfg.paw, s_cfg.cpi);
    paw3395_set_mode(s_cfg.paw, s_cfg.mode);
    paw3395_set_lift_cut(s_cfg.paw, s_cfg.lift);
    paw3395_motion_start();

    s_active = false;

    LOG_I("AIR", "exit: imu suspended, paw re-init cpi=%u", (unsigned)s_cfg.cpi);
}

void air_mouse_init(const air_mouse_cfg_t *cfg)
{
    s_cfg         = *cfg;
    s_active      = false;
    s_combo_held  = false;
    s_combo_armed = false;
    motor_off();

    if (s_cfg.odr_idx < AIR_ODR_COUNT) {
        s_odr_idx = s_cfg.odr_idx;
    }
    if (s_cfg.sens_idx < AIR_SENS_COUNT) {
        s_sens_idx = s_cfg.sens_idx;
    }

    const attitude_cfg_t acfg = ATTITUDE_CFG_DEFAULT;
    attitude_init(&s_att, &acfg);

    pointer_cfg_t pcfg = POINTER_CFG_DEFAULT;
    pcfg.counts_x = (float)s_sens_x[s_sens_idx];
    pcfg.counts_y = (float)s_sens_y[s_sens_idx];
    /* Fixed on the bench: yaw (about the gravity axis) is horizontal, the
     * pitch tilt is vertical, and both signs match the HID report. */
    pcfg.swap     = 0u;
    pcfg.invert_x = 1u;
    pcfg.invert_y = 1u;
    pointer_init(&s_ptr, &pcfg);

    LOG_I("AIR", "ready (side1+side2 hold %u ms)", (unsigned)AIR_MOUSE_HOLD_MS);
}

bool air_mouse_active(void)
{
    return s_active;
}

bool air_mouse_combo_held(void)
{
    return s_combo_held;
}

void air_mouse_poll(uint32_t now_ms, uint8_t raw_buttons, uint8_t buttons)
{
    const uint8_t combo_mask = (uint8_t)(HID_BTN_BACK | HID_BTN_FWD);
    const bool    combo      = ((buttons & combo_mask) == combo_mask);

    s_now_ms      = now_ms;
    s_raw_buttons = raw_buttons;

    if (combo != s_combo_held) {
        s_combo_held = combo;
        s_combo_ms   = now_ms;

        if (combo) {
            s_combo_armed = false;   /* fresh hold re-arms the 2 s timer */
        }
    } else if (combo && !s_combo_armed && (now_ms - s_combo_ms >= AIR_MOUSE_HOLD_MS)) {
        s_combo_armed = true;

        if (s_active) {
            air_mouse_exit(now_ms);
        } else {
            air_mouse_enter(now_ms);
        }
    }
}

void air_mouse_read_motion(int16_t *dx, int16_t *dy)
{
    struct bmi2_sens_data d;

    *dx = 0;
    *dy = 0;

    if (bmi2_get_sensor_data(&d, s_cfg.bmi) != BMI2_OK) {
        return;
    }

    /* The gyro delivers a fresh sample every few scans; acting on the same one
     * repeatedly would integrate it repeatedly and race the pointer. */
    if (!(d.status & BMI2_DRDY_GYR)) {
        return;
    }

    const float dt = air_mouse_dt_from_sensor_time(d.sens_time);
    const float gx = (float)d.gyr.x / AIR_GYRO_LSB_PER_DPS;
    const float gy = (float)d.gyr.y / AIR_GYRO_LSB_PER_DPS;
    const float gz = (float)d.gyr.z / AIR_GYRO_LSB_PER_DPS;
    const float ax = (float)d.acc.x / AIR_ACCEL_LSB_PER_G;
    const float ay = (float)d.acc.y / AIR_ACCEL_LSB_PER_G;
    const float az = (float)d.acc.z / AIR_ACCEL_LSB_PER_G;

    if (!attitude_update(&s_att, gx, gy, gz, ax, ay, az, dt)) {
        s_was_ready = false;
        return;   /* still measuring the zero-bias */
    }

    if (!s_was_ready) {
        /* First ready sample: the entry pose becomes the neutral reference. */
        s_was_ready = true;
        pointer_rebase(&s_ptr);
    }

    pointer_update(&s_ptr,
                   attitude_roll_deg(&s_att),
                   attitude_yaw_deg(&s_att),
                   attitude_pitch_deg(&s_att),
                   s_raw_buttons, s_now_ms, dt,
                   dx, dy);
}
