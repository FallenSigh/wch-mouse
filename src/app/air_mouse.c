/*
 * src/app/air_mouse.c - gyro-driven pointer mode with no power cycle.
 *
 * The optical sensor and the IMU never both own the cursor, so entering the
 * mode powers the PAW3395 down and brings the BMI270 gyro up; leaving it
 * suspends the gyro and re-runs the full optical bring-up. The BMI270 API in
 * this vendor tree has no suspend call, so "IMU suspend" is
 * bmi270_sensor_disable(BMI2_GYRO).
 *
 * Cursor motion is the gyro rate integrated over the 1 kHz scan and divided
 * down to pixels. The zero-rate bias is measured on entry (the user is holding
 * the mouse still to press the combo) and subtracted, so the pointer does not
 * creep while the mouse is stationary.
 */

#include "air_mouse.h"

#include "bmi2.h"
#include "bmi2_defs.h"
#include "bmi270.h"
#include "log.h"
#include "motor.h"
#include "mouse.h"
#include "paw3395_port.h"

/* Side-1 (PR_SW4/BACK) + side-2 (PR_SW5/FWD) held this long enter/leave. */
#define AIR_MOUSE_HOLD_MS        2000u

/* Gyro data-ready samples averaged into the zero-bias at entry, after the
 * start-up samples below are discarded. The gyro runs at 200 Hz (the ODR set
 * in air_mouse_gyro_on), so 16 averaged samples is 80 ms of "hold still";
 * motion reporting starts once the bias is known. */
#define AIR_MOUSE_CALIB_SAMPLES  16u

/* Start-up samples dropped before averaging: the gyro needs ~30 ms after its
 * power mode is set before the output settles. */
#define AIR_MOUSE_CALIB_WARMUP   8u

/* Gyro counts accumulated per data-ready sample, divided down to pixels with
 * the remainder carried so slow motion is not rounded away. Derived from the
 * wanted sensitivity S in pixels/degree and the 5 ms sample period:
 *
 *     divisor = 16.4 counts/dps / (S * 0.005 s)
 *
 * so the default ~15 px/deg gives 16.4 / 0.075 = 218. Tune to taste. */
#ifndef AIR_MOUSE_GYRO_DIV
#define AIR_MOUSE_GYRO_DIV       218
#endif

/* Below this many raw counts a gyro axis is treated as still, keeping residual
 * bias noise from creeping the pointer. 4 counts ~ 0.24 dps. */
#ifndef AIR_MOUSE_DEADZONE
#define AIR_MOUSE_DEADZONE       4
#endif

/* Board mounting: the BMI270 is mounted with its X axis along the mouse's
 * vertical and its Y axis across it, so yaw (left/right) is gyro X and pitch
 * (up/down) is gyro Y; gyro Z is roll and is not used. Both signs are negative
 * because a positive X rate is a left turn and a positive Y rate raises the
 * nose. Flip either sign if a board is mounted the other way round. */
#define AIR_MOUSE_YAW_SIGN       (-1)
#define AIR_MOUSE_PITCH_SIGN     (-1)

static air_mouse_cfg_t s_cfg;

static bool     s_active;
static bool     s_combo_held;
static bool     s_combo_armed;
static uint32_t s_combo_ms;

static uint8_t s_calib_left;
static int32_t s_calib_sum_x;
static int32_t s_calib_sum_y;
static int32_t s_calib_sum_z;
static int16_t s_bias_x;
static int16_t s_bias_y;
static int16_t s_bias_z;

static int32_t s_frac_x;
static int32_t s_frac_y;

static uint32_t s_haptic_until;

static void air_mouse_haptic(uint32_t now_ms, uint8_t pct, uint32_t ms)
{
    motor_set(pct);
    s_haptic_until = now_ms + ms;
}

static void air_mouse_gyro_on(void)
{
    struct bmi2_sens_config cfg = { 0 };
    uint8_t                gyr = BMI2_GYRO;

    cfg.type                = BMI2_GYRO;
    cfg.cfg.gyr.odr         = BMI2_GYR_ODR_200HZ;
    cfg.cfg.gyr.bwp         = BMI2_GYR_NORMAL_MODE;
    cfg.cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;
    cfg.cfg.gyr.noise_perf  = BMI2_POWER_OPT_MODE;
    cfg.cfg.gyr.range       = BMI2_GYR_RANGE_2000;
    cfg.cfg.gyr.ois_range   = BMI2_GYR_OIS_2000;

    if (bmi2_set_sensor_config(&cfg, 1, s_cfg.bmi) != BMI2_OK) {
        LOG_W("AIR", "gyro config failed");
    }

    if (bmi270_sensor_enable(&gyr, 1, s_cfg.bmi) != BMI2_OK) {
        LOG_W("AIR", "gyro enable failed");
    }
}

static void air_mouse_enter(uint32_t now_ms)
{
    /* Hand the cursor over: optical sensor down, gyro up, then start measuring
     * the zero-rate bias before any motion is reported. The optical sensor's
     * IRQ-driven read is parked first so its blocking shutdown write cannot
     * collide with an in-flight DMA burst. */
    paw3395_motion_stop();
    paw3395_shutdown(s_cfg.paw);
    air_mouse_gyro_on();

    s_calib_left  = AIR_MOUSE_CALIB_SAMPLES + AIR_MOUSE_CALIB_WARMUP;
    s_calib_sum_x = 0;
    s_calib_sum_y = 0;
    s_calib_sum_z = 0;
    s_frac_x      = 0;
    s_frac_y      = 0;
    s_active      = true;

    LOG_I("AIR", "enter: paw off, gyro on, calibrating %u samples",
          (unsigned)AIR_MOUSE_CALIB_SAMPLES);
    air_mouse_haptic(now_ms, 70u, 80u);
}

static void air_mouse_exit(uint32_t now_ms)
{
    uint8_t gyr = BMI2_GYRO;

    /* IMU suspended: nothing reads the gyro until the next entry. */
    if (bmi270_sensor_disable(&gyr, 1, s_cfg.bmi) != BMI2_OK) {
        LOG_W("AIR", "gyro disable failed");
    }

    /* The optical sensor comes back through the full bring-up, which resets
     * the driver's geometry, so re-apply the baseline the caller gave us. */
    paw3395_init(s_cfg.paw);
    paw3395_set_cpi(s_cfg.paw, s_cfg.cpi);
    paw3395_set_mode(s_cfg.paw, s_cfg.mode);
    paw3395_set_lift_cut(s_cfg.paw, s_cfg.lift);
    paw3395_motion_start();

    s_active     = false;
    s_calib_left = 0;

    LOG_I("AIR", "exit: gyro suspended, paw re-init cpi=%u", (unsigned)s_cfg.cpi);
    air_mouse_haptic(now_ms, 35u, 120u);
}

void air_mouse_init(const air_mouse_cfg_t *cfg)
{
    s_cfg          = *cfg;
    s_active       = false;
    s_combo_held   = false;
    s_combo_armed  = false;
    s_calib_left   = 0;
    s_haptic_until = 0;

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

void air_mouse_poll(uint32_t now_ms, uint8_t buttons)
{
    const uint8_t combo_mask = (uint8_t)(HID_BTN_BACK | HID_BTN_FWD);
    const bool    combo      = ((buttons & combo_mask) == combo_mask);

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

    /* Non-blocking haptic pulse: brake once the window has elapsed. */
    if (s_haptic_until != 0u && (int32_t)(now_ms - s_haptic_until) >= 0) {
        s_haptic_until = 0u;
        motor_brake();
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

    /* The gyro delivers a fresh sample every 5 scans; acting on the same one
     * five times would integrate it five times and race the pointer. */
    if (!(d.status & BMI2_DRDY_GYR)) {
        return;
    }

    if (s_calib_left > 0u) {
        /* Only the samples past the start-up transient feed the average. */
        if (s_calib_left <= AIR_MOUSE_CALIB_SAMPLES) {
            s_calib_sum_x += d.gyr.x;
            s_calib_sum_y += d.gyr.y;
            s_calib_sum_z += d.gyr.z;
        }

        if (--s_calib_left == 0u) {
            s_bias_x = (int16_t)(s_calib_sum_x / (int32_t)AIR_MOUSE_CALIB_SAMPLES);
            s_bias_y = (int16_t)(s_calib_sum_y / (int32_t)AIR_MOUSE_CALIB_SAMPLES);
            s_bias_z = (int16_t)(s_calib_sum_z / (int32_t)AIR_MOUSE_CALIB_SAMPLES);
            LOG_I("AIR", "zero-bias x=%d y=%d z=%d", s_bias_x, s_bias_y, s_bias_z);
        }
        return;   /* no cursor motion until the bias is known */
    }

    int32_t yaw   = (int32_t)AIR_MOUSE_YAW_SIGN * ((int32_t)d.gyr.x - s_bias_x);
    int32_t pitch = (int32_t)AIR_MOUSE_PITCH_SIGN * ((int32_t)d.gyr.y - s_bias_y);

    if (yaw > -AIR_MOUSE_DEADZONE && yaw < AIR_MOUSE_DEADZONE) {
        yaw = 0;
    }
    if (pitch > -AIR_MOUSE_DEADZONE && pitch < AIR_MOUSE_DEADZONE) {
        pitch = 0;
    }

    s_frac_x += yaw;
    s_frac_y += pitch;

    int32_t px = s_frac_x / AIR_MOUSE_GYRO_DIV;
    int32_t py = s_frac_y / AIR_MOUSE_GYRO_DIV;
    s_frac_x -= px * AIR_MOUSE_GYRO_DIV;
    s_frac_y -= py * AIR_MOUSE_GYRO_DIV;

    if (px > 32767) {
        px = 32767;
    } else if (px < -32768) {
        px = -32768;
    }
    if (py > 32767) {
        py = 32767;
    } else if (py < -32768) {
        py = -32768;
    }

    *dx = (int16_t)px;
    *dy = (int16_t)py;
}
