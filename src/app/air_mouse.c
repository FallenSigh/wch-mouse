/*
 * src/app/air_mouse.c - gyro-driven pointer mode with no power cycle.
 *
 * The optical sensor and the IMU never both own the cursor, so entering the
 * mode powers the PAW3395 down and brings the BMI270 gyro + accelerometer up;
 * leaving it disables both sensors and re-runs the full optical bring-up. The
 * BMI270 API in this vendor tree has no suspend call, so "IMU suspend" is
 * bmi270_sensor_disable({BMI2_GYRO, BMI2_ACCEL}).
 *
 * Cursor motion comes from the fused attitude: the gyro is integrated into a
 * Mahony quaternion and corrected by the accelerometer, the resulting roll/yaw
 * are unwrapped and measured against a neutral pose, One Euro filtered, and
 * their frame-to-frame change is scaled to relative mouse counts. The zero-rate
 * bias is measured on entry and refined online (ZARU); a click guard freezes
 * the pointer while a button is held; and the middle button is a clutch (hold
 * to reposition the hand without moving the cursor).
 */

#include "air_mouse.h"

#include "bmi2.h"
#include "bmi2_defs.h"
#include "bmi270.h"
#include "log.h"
#include "motor.h"
#include "mouse.h"
#include "paw3395_port.h"

#include <math.h>

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

/* Active presets; the caller seeds these through air_mouse_cfg_t. */
static uint8_t s_sens_idx = 3u;
static uint8_t s_odr_idx  = 3u;
static float   s_counts_x = 36.0f;
static float   s_counts_y = 32.0f;

static air_mouse_cfg_t s_cfg;

static bool     s_active;
static bool     s_combo_held;
static bool     s_combo_armed;
static uint32_t s_combo_ms;

static uint8_t s_calib_left;
static int32_t s_calib_sum_x;
static int32_t s_calib_sum_y;
static int32_t s_calib_sum_z;
static int32_t s_calib_acc_x;
static int32_t s_calib_acc_y;
static int32_t s_calib_acc_z;
static float s_bias_x;
static float s_bias_y;
static float s_bias_z;

/* Zero-rate update: while the gyro reads still, its residual is bias, so it is
 * folded into the estimate; a confirmed-still sample is then fed as zero to the
 * attitude filter so residual bias cannot integrate into yaw. */
#define AIR_ZARU_GYRO_DPS     0.5f
#define AIR_ZARU_ACCEL_TOL_G  0.08f
#define AIR_ZARU_CONFIRM      100u    /* ~0.5 s at 200 Hz */
#define AIR_ZARU_TAU_S        4.0f

static uint16_t s_zaru_count;
static float    s_zaru_sum_x;
static float    s_zaru_sum_y;
static float    s_zaru_sum_z;
static bool     s_zaru_active;

/* Last BMI270 sensor-time sample (39.0625 us ticks), for a real integration
 * step instead of the nominal 5 ms. */
static uint32_t s_last_st;
static bool     s_have_st;

/* Relative-count accumulator (fractional part kept) and the previous filtered
 * pointer angle the deltas are taken against. */
static float s_pend_x;
static float s_pend_y;
static float s_last_fx;
static float s_last_fy;
static bool  s_angle_ready;

/* Attitude as a scalar-first Hamilton quaternion (w,x,y,z): the rotation from
 * the body frame to a gravity-referenced frame. B1 seeds it from the
 * accelerometer; B2 integrates the gyro and corrects it with the accel. */
static float s_q0 = 1.0f;
static float s_q1;
static float s_q2;
static float s_q3;

/* Unwrapped roll/yaw. The Euler angles are wrapped to [-180,180], so a turn
 * through that boundary would jump; the difference is accumulated here to keep
 * the pointer motion continuous across it. */
static bool  s_cont_ready;
static float s_last_roll;
static float s_last_yaw;
static float s_cont_roll;
static float s_cont_yaw;

/* Neutral pose captured on entry: the pointer angles are measured relative to
 * it, so the pose held when the mode is entered becomes "no motion". */
static bool  s_neutral_pending;
static float s_neutral_pitch;
static float s_neutral_yaw;

/* Click guard: a mouse-button press freezes the pointer so the click cannot
 * drag the cursor, and the pose is re-based on release so motion resumes
 * without a jump. Holding a button and rotating past the unlock angle resumes
 * motion, which is what makes drag and FPS aiming work. It is driven by the
 * raw button mask so the freeze happens on the press edge, not after debounce. */
#define AIR_GUARD_SETTLE_MS   35u
#define AIR_GUARD_UNLOCK_DEG  1.0f

static uint32_t s_now_ms;
static uint8_t  s_guard_raw;
static uint8_t  s_guard_raw_prev;
static bool     s_guard_frozen;
static bool     s_guard_settling;
static uint32_t s_guard_settle_at;
static float    s_guard_start_yaw;
static float    s_guard_start_pitch;

/* Middle button as a clutch: holding it freezes the pointer so the hand can be
 * repositioned (rotated freely) without moving the cursor, and releasing it
 * resumes from the new pose. */
static uint8_t s_mid_raw_prev;
static bool    s_clutch_held;

#define AIR_RAD2DEG  (57.29577951308232f)
#define AIR_DEG2RAD  (0.017453292519943295f)

static float air_mouse_clampf(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

/* At rest the accelerometer points up (opposite gravity), so on its own it
 * fixes roll and pitch; yaw is unknowable without a magnetometer and stays 0.
 * Both angles are scale-invariant, so raw counts are fine here. */
static void air_mouse_attitude_from_accel(float ax, float ay, float az)
{
    const float roll  = atan2f(ay, az);
    const float pitch = atan2f(-ax, sqrtf(ay * ay + az * az));
    const float cr = cosf(roll * 0.5f);
    const float sr = sinf(roll * 0.5f);
    const float cp = cosf(pitch * 0.5f);
    const float sp = sinf(pitch * 0.5f);

    s_q0 = cr * cp;
    s_q1 = sr * cp;
    s_q2 = cr * sp;
    s_q3 = -sr * sp;
}

static float air_mouse_roll_deg(void)
{
    return atan2f(2.0f * (s_q0 * s_q1 + s_q2 * s_q3),
                  1.0f - 2.0f * (s_q1 * s_q1 + s_q2 * s_q2)) * AIR_RAD2DEG;
}

static float air_mouse_pitch_deg(void)
{
    return asinf(air_mouse_clampf(2.0f * (s_q0 * s_q2 - s_q3 * s_q1), -1.0f, 1.0f))
           * AIR_RAD2DEG;
}

static float air_mouse_yaw_deg(void)
{
    return atan2f(2.0f * (s_q0 * s_q3 + s_q1 * s_q2),
                  1.0f - 2.0f * (s_q2 * s_q2 + s_q3 * s_q3)) * AIR_RAD2DEG;
}

/* Mahony fusion. Kp is the accelerometer blend; Ki stays 0 because yaw is
 * unobservable without a magnetometer, so an integral term would only pump
 * false feedback into it. */
#define AIR_MAHONY_KP         1.0f

/* Scaling for the configured ranges: +-2000 dps -> 16.4 LSB/dps, +-4 g ->
 * 8192 LSB/g. The gyro delivers one data-ready sample every 5 ms at 200 Hz. */
#define AIR_GYRO_LSB_PER_DPS  16.4f
#define AIR_ACCEL_LSB_PER_G   8192.0f
#define AIR_GYRO_DT_S         0.005f

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

/* Zero-rate update, using the state declared above. On a still sample the
 * residual is accumulated; once confirmed, its mean is added to the bias in one
 * go and later samples adapt it slowly. A confirmed-still sample is returned as
 * zero so the attitude filter cannot integrate the residual into yaw. */
static void air_mouse_zaru(float gx, float gy, float gz,
                           float ax, float ay, float az,
                           float dt,
                           float *out_x, float *out_y, float *out_z)
{
    const float gx_dps = gx / AIR_GYRO_LSB_PER_DPS;
    const float gy_dps = gy / AIR_GYRO_LSB_PER_DPS;
    const float gz_dps = gz / AIR_GYRO_LSB_PER_DPS;
    const float gyro_mag2  = gx_dps * gx_dps + gy_dps * gy_dps + gz_dps * gz_dps;
    const float accel_mag2 = (ax * ax + ay * ay + az * az) /
                             (AIR_ACCEL_LSB_PER_G * AIR_ACCEL_LSB_PER_G);

    const bool still = (gyro_mag2 <= AIR_ZARU_GYRO_DPS * AIR_ZARU_GYRO_DPS)
                    && (accel_mag2 >= 0.8464f)      /* (1 - tol)^2 */
                    && (accel_mag2 <= 1.1664f);     /* (1 + tol)^2 */

    if (!still) {
        s_zaru_count  = 0u;
        s_zaru_sum_x  = 0.0f;
        s_zaru_sum_y  = 0.0f;
        s_zaru_sum_z  = 0.0f;
        s_zaru_active = false;
        *out_x = gx;
        *out_y = gy;
        *out_z = gz;
        return;
    }

    if (!s_zaru_active) {
        s_zaru_sum_x += gx;
        s_zaru_sum_y += gy;
        s_zaru_sum_z += gz;
        if (s_zaru_count < AIR_ZARU_CONFIRM) {
            ++s_zaru_count;
        }
        if (s_zaru_count < AIR_ZARU_CONFIRM) {
            *out_x = gx;
            *out_y = gy;
            *out_z = gz;
            return;
        }

        const float inv = 1.0f / (float)s_zaru_count;
        s_bias_x += s_zaru_sum_x * inv;
        s_bias_y += s_zaru_sum_y * inv;
        s_bias_z += s_zaru_sum_z * inv;
        s_zaru_active = true;
    } else {
        const float lambda = dt / (AIR_ZARU_TAU_S + dt);
        s_bias_x += lambda * gx;
        s_bias_y += lambda * gy;
        s_bias_z += lambda * gz;
    }

    *out_x = 0.0f;
    *out_y = 0.0f;
    *out_z = 0.0f;
}

/* One Mahony step: integrate the gyro into the quaternion, then correct the
 * tilt with the accelerometer. The accelerometer is used only as a direction,
 * and only when its magnitude is within +-15% of 1 g, so linear acceleration
 * during a swing cannot corrupt the tilt. */
static void air_mouse_attitude_update(float gx, float gy, float gz,
                                      int16_t ax, int16_t ay, int16_t az,
                                      float dt)
{
    const float deg_to_rad = AIR_DEG2RAD;
    float wx = gx / AIR_GYRO_LSB_PER_DPS * deg_to_rad;
    float wy = gy / AIR_GYRO_LSB_PER_DPS * deg_to_rad;
    float wz = gz / AIR_GYRO_LSB_PER_DPS * deg_to_rad;

    const float axf = (float)ax;
    const float ayf = (float)ay;
    const float azf = (float)az;
    const float a2 = axf * axf + ayf * ayf + azf * azf;
    const float a2_g = a2 / (AIR_ACCEL_LSB_PER_G * AIR_ACCEL_LSB_PER_G);

    if (a2_g >= 0.7225f && a2_g <= 1.3225f) {   /* 0.85^2 .. 1.15^2 */
        const float inv = 1.0f / sqrtf(a2);
        const float nx = axf * inv;
        const float ny = ayf * inv;
        const float nz = azf * inv;

        /* Predicted "up" direction in the body frame, halved. */
        const float half_vx = s_q1 * s_q3 - s_q0 * s_q2;
        const float half_vy = s_q0 * s_q1 + s_q2 * s_q3;
        const float half_vz = s_q0 * s_q0 - 0.5f + s_q3 * s_q3;

        /* Cross product of measured and predicted up, halved: the tilt error. */
        const float half_ex = ny * half_vz - nz * half_vy;
        const float half_ey = nz * half_vx - nx * half_vz;
        const float half_ez = nx * half_vy - ny * half_vx;

        wx += 2.0f * AIR_MAHONY_KP * half_ex;
        wy += 2.0f * AIR_MAHONY_KP * half_ey;
        wz += 2.0f * AIR_MAHONY_KP * half_ez;
    }

    const float half_dt = 0.5f * dt;
    const float qa = s_q0;
    const float qb = s_q1;
    const float qc = s_q2;

    s_q0 += (-qb * wx - qc * wy - s_q3 * wz) * half_dt;
    s_q1 += ( qa * wx + qc * wz - s_q3 * wy) * half_dt;
    s_q2 += ( qa * wy - qb * wz + s_q3 * wx) * half_dt;
    s_q3 += ( qa * wz + qb * wy - qc * wx) * half_dt;

    const float q2 = s_q0 * s_q0 + s_q1 * s_q1 + s_q2 * s_q2 + s_q3 * s_q3;
    if (q2 > 0.000001f) {
        const float inv = 1.0f / sqrtf(q2);
        s_q0 *= inv;
        s_q1 *= inv;
        s_q2 *= inv;
        s_q3 *= inv;
    }
}

/* Fold an angle into [-180,180] so a step across the wrap is seen as the small
 * turn it really is instead of a ~360 degree jump. */
static float air_mouse_wrap180(float deg)
{
    while (deg > 180.0f) {
        deg -= 360.0f;
    }
    while (deg < -180.0f) {
        deg += 360.0f;
    }
    return deg;
}

/* Accumulate the wrapped roll/yaw into continuous angles. The first sample
 * after entry only establishes the baseline. */
static void air_mouse_update_continuous(void)
{
    const float roll = air_mouse_roll_deg();
    const float yaw  = air_mouse_yaw_deg();

    if (!s_cont_ready) {
        s_cont_roll  = roll;
        s_cont_yaw   = yaw;
        s_cont_ready = true;
    } else {
        s_cont_roll += air_mouse_wrap180(roll - s_last_roll);
        s_cont_yaw  += air_mouse_wrap180(yaw - s_last_yaw);
    }

    s_last_roll = roll;
    s_last_yaw  = yaw;
}

/* Output axis mapping, fixed on the bench with AIR_MOUSE_SWAP/INVERT. At rest
 * the BMI270's Z is the gravity axis, so yaw (about Z) is the horizontal aim.
 * The vertical aim is the pitch tilt (about the horizontal axis across the
 * mouse), verified on the bench: tilting the front moved acc.x, not acc.y. */
#define AIR_MOUSE_SWAP       0
#define AIR_MOUSE_INVERT_X   1
#define AIR_MOUSE_INVERT_Y   1

static void air_mouse_pointer_angles(float *x_deg, float *y_deg)
{
    float x = s_cont_yaw - s_neutral_yaw;
    float y = air_mouse_pitch_deg() - s_neutral_pitch;

    if (AIR_MOUSE_SWAP) {
        const float t = x;
        x = y;
        y = t;
    }
    if (AIR_MOUSE_INVERT_X) {
        x = -x;
    }
    if (AIR_MOUSE_INVERT_Y) {
        y = -y;
    }

    *x_deg = x;
    *y_deg = y;
}

/* One Euro filter: a low-pass whose cutoff rises with the estimated angular
 * speed, so it smooths hard while still and barely lags while moving. */
#define AIR_FILTER_MIN_CUTOFF_HZ   3.0f
#define AIR_FILTER_BETA            0.18f
#define AIR_FILTER_DERIV_CUTOFF_HZ 2.0f

typedef struct {
    bool  initialized;
    float last_raw;
    float filtered;
    float deriv;
} air_one_euro_t;

static air_one_euro_t s_filter_x;
static air_one_euro_t s_filter_y;

static float air_one_euro_alpha(float cutoff_hz, float dt)
{
    const float tau = 1.0f / (6.283185307179586f * cutoff_hz);
    return dt / (tau + dt);
}

static void air_one_euro_reset(air_one_euro_t *f, float value)
{
    f->initialized = true;
    f->last_raw    = value;
    f->filtered    = value;
    f->deriv       = 0.0f;
}

static float air_one_euro(air_one_euro_t *f, float value, float dt)
{
    if (!f->initialized) {
        air_one_euro_reset(f, value);
        return value;
    }

    const float raw_deriv = (value - f->last_raw) / dt;
    f->last_raw = value;

    const float deriv_alpha = air_one_euro_alpha(AIR_FILTER_DERIV_CUTOFF_HZ, dt);
    f->deriv += deriv_alpha * (raw_deriv - f->deriv);

    const float cutoff = AIR_FILTER_MIN_CUTOFF_HZ + AIR_FILTER_BETA * fabsf(f->deriv);
    const float value_alpha = air_one_euro_alpha(cutoff, dt);
    f->filtered += value_alpha * (value - f->filtered);
    return f->filtered;
}

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
    s_counts_x = (float)s_sens_x[idx];
    s_counts_y = (float)s_sens_y[idx];
}

void air_mouse_set_odr(uint8_t idx)
{
    if (idx >= AIR_ODR_COUNT) {
        return;
    }

    s_odr_idx = idx;

    if (s_active) {
        const uint8_t sensors[2] = { BMI2_GYRO, BMI2_ACCEL };

        /* Re-enable at the new rate. The bias and the attitude are kept; the
         * sensor-time baseline and the ZARU confirmation restart because the
         * sample cadence changed. */
        (void)bmi270_sensor_disable(sensors, 2, s_cfg.bmi);
        air_mouse_imu_on();
        s_have_st     = false;
        s_zaru_count  = 0u;
        s_zaru_active = false;
    }
}

static void air_mouse_enter(uint32_t now_ms)
{
    /* Fire the feedback first and do not wait for it: the hand-off below blocks
     * for tens of ms, so a blocking buzz would only start afterwards. */
    motor_pulse(MOTOR_MODE_SWITCH_MS);

    /* Hand the cursor over: optical sensor down, gyro up, then start measuring
     * the zero-rate bias before any motion is reported. The optical sensor's
     * IRQ-driven read is parked first so its blocking shutdown write cannot
     * collide with an in-flight DMA burst. */
    paw3395_motion_stop();
    paw3395_shutdown(s_cfg.paw);
    air_mouse_imu_on();

    s_calib_left  = AIR_MOUSE_CALIB_SAMPLES + AIR_MOUSE_CALIB_WARMUP;
    s_calib_sum_x = 0;
    s_calib_sum_y = 0;
    s_calib_sum_z = 0;
    s_calib_acc_x = 0;
    s_calib_acc_y = 0;
    s_calib_acc_z = 0;
    s_pend_x      = 0.0f;
    s_pend_y      = 0.0f;
    s_cont_ready  = false;
    s_neutral_pending = false;
    s_guard_frozen    = false;
    s_guard_settling  = false;
    s_zaru_count  = 0u;
    s_zaru_active = false;
    s_have_st     = false;
    s_mid_raw_prev = 0u;
    s_clutch_held = false;
    s_active      = true;

    LOG_I("AIR", "enter: paw off, gyro on, calibrating %u samples",
          (unsigned)AIR_MOUSE_CALIB_SAMPLES);
}

static void air_mouse_exit(uint32_t now_ms)
{
    const uint8_t sensors[2] = { BMI2_GYRO, BMI2_ACCEL };

    /* Same again: the optical bring-up below blocks for tens of ms. */
    motor_pulse(MOTOR_MODE_SWITCH_MS);

    /* IMU suspended: nothing reads the gyro until the next entry. */
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

    s_active     = false;
    s_calib_left = 0;

    LOG_I("AIR", "exit: gyro suspended, paw re-init cpi=%u", (unsigned)s_cfg.cpi);
}

void air_mouse_init(const air_mouse_cfg_t *cfg)
{
    s_cfg          = *cfg;
    s_active       = false;
    s_combo_held   = false;
    s_combo_armed  = false;
    s_calib_left   = 0;
    motor_off();

    air_mouse_set_sensitivity(s_cfg.sens_idx);
    if (s_cfg.odr_idx < AIR_ODR_COUNT) {
        s_odr_idx = s_cfg.odr_idx;
    }

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

static void air_mouse_guard_freeze(void)
{
    s_guard_frozen      = true;
    s_guard_settling    = false;
    s_pend_x            = 0.0f;
    s_pend_y            = 0.0f;
    s_guard_start_yaw   = s_cont_yaw;
    s_guard_start_pitch = air_mouse_pitch_deg();
}

void air_mouse_poll(uint32_t now_ms, uint8_t raw_buttons, uint8_t buttons)
{
    const uint8_t combo_mask = (uint8_t)(HID_BTN_BACK | HID_BTN_FWD);
    const bool    combo      = ((buttons & combo_mask) == combo_mask);

    s_now_ms    = now_ms;
    s_guard_raw = (uint8_t)(raw_buttons & (uint8_t)(HID_BTN_LEFT | HID_BTN_RIGHT));

    if ((s_guard_raw != 0u) && (s_guard_raw_prev == 0u)) {
        air_mouse_guard_freeze();
    } else if ((s_guard_raw == 0u) && (s_guard_raw_prev != 0u)) {
        /* Released: hold the freeze through the settle window so the rebound
         * of the finger does not move the cursor. */
        s_guard_settling  = true;
        s_guard_settle_at = now_ms + AIR_GUARD_SETTLE_MS;
    }
    s_guard_raw_prev = s_guard_raw;

    const uint8_t mid_raw = (uint8_t)(raw_buttons & HID_BTN_MID);
    if ((mid_raw != 0u) && (s_mid_raw_prev == 0u) && s_active) {
        s_clutch_held = true;
        air_mouse_guard_freeze();
    } else if ((mid_raw == 0u) && (s_mid_raw_prev != 0u) && s_clutch_held) {
        /* Release: resume from the pose reached while frozen, re-based so the
         * release itself does not move the cursor. */
        s_clutch_held     = false;
        s_guard_frozen    = false;
        s_guard_settling  = false;
        s_neutral_pending = true;
    }
    s_mid_raw_prev = mid_raw;

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
            s_calib_acc_x += d.acc.x;
            s_calib_acc_y += d.acc.y;
            s_calib_acc_z += d.acc.z;
        }

        if (--s_calib_left == 0u) {
            s_bias_x = (float)s_calib_sum_x / (float)AIR_MOUSE_CALIB_SAMPLES;
            s_bias_y = (float)s_calib_sum_y / (float)AIR_MOUSE_CALIB_SAMPLES;
            s_bias_z = (float)s_calib_sum_z / (float)AIR_MOUSE_CALIB_SAMPLES;

            /* Seed the attitude from the averaged gravity so the gyro
             * integration starts from the real tilt; yaw starts at 0. */
            air_mouse_attitude_from_accel((float)s_calib_acc_x,
                                          (float)s_calib_acc_y,
                                          (float)s_calib_acc_z);
            s_neutral_pending = true;

            LOG_I("AIR", "zero-bias x100 %d %d %d",
                  (int)(s_bias_x * 100.0f), (int)(s_bias_y * 100.0f),
                  (int)(s_bias_z * 100.0f));
        }
        return;   /* no cursor motion until the bias is known */
    }

    /* Bias-correct, then let ZARU fold a still residual back into the bias and
     * return zero for a confirmed-still sample. */
    const float dt = air_mouse_dt_from_sensor_time(d.sens_time);
    float zgx;
    float zgy;
    float zgz;

    air_mouse_zaru((float)d.gyr.x - s_bias_x,
                   (float)d.gyr.y - s_bias_y,
                   (float)d.gyr.z - s_bias_z,
                   (float)d.acc.x, (float)d.acc.y, (float)d.acc.z,
                   dt, &zgx, &zgy, &zgz);

    /* Fused attitude: the gyro drives it, the accelerometer anchors the tilt. */
    air_mouse_attitude_update(zgx, zgy, zgz, d.acc.x, d.acc.y, d.acc.z, dt);
    air_mouse_update_continuous();

    if (s_guard_frozen) {
        if (s_clutch_held) {
            return;   /* clutch: frozen for as long as the middle button is held */
        }

        const uint8_t mouse_mask = (uint8_t)(HID_BTN_LEFT | HID_BTN_RIGHT);
        const bool    held       = (s_guard_raw & mouse_mask) != 0u;
        const float   dyaw       = s_cont_yaw - s_guard_start_yaw;
        const float   dpitch     = air_mouse_pitch_deg() - s_guard_start_pitch;

        if (held && ((dyaw * dyaw + dpitch * dpitch) >=
                     (AIR_GUARD_UNLOCK_DEG * AIR_GUARD_UNLOCK_DEG))) {
            /* Deliberate rotation with a button held: resume for a drag or FPS
             * aim, re-based so the unlock itself does not move the cursor. */
            s_guard_frozen    = false;
            s_guard_settling  = false;
            s_neutral_pending = true;
        } else if (!held && s_guard_settling &&
                   ((int32_t)(s_now_ms - s_guard_settle_at) >= 0)) {
            s_guard_frozen    = false;
            s_guard_settling  = false;
            s_neutral_pending = true;
        } else {
            return;   /* still guarded: emit no motion */
        }
    }

    if (s_neutral_pending) {
        s_neutral_yaw     = s_cont_yaw;
        s_neutral_pitch   = air_mouse_pitch_deg();
        s_neutral_pending = false;
        air_one_euro_reset(&s_filter_x, 0.0f);
        air_one_euro_reset(&s_filter_y, 0.0f);
        s_angle_ready     = false;
    }

    float px_deg;
    float py_deg;
    air_mouse_pointer_angles(&px_deg, &py_deg);

    const float fx = air_one_euro(&s_filter_x, px_deg, dt);
    const float fy = air_one_euro(&s_filter_y, py_deg, dt);

    /* The filtered pointer angle is gravity-anchored, so its frame-to-frame
     * change is the motion; scale it to relative mouse counts. */
    if (!s_angle_ready) {
        s_last_fx     = fx;
        s_last_fy     = fy;
        s_angle_ready = true;
        return;
    }

    s_pend_x += (fx - s_last_fx) * s_counts_x;
    s_pend_y += (fy - s_last_fy) * s_counts_y;
    s_last_fx = fx;
    s_last_fy = fy;

    float ex = s_pend_x;
    float ey = s_pend_y;

    if (ex > 32767.0f) {
        ex = 32767.0f;
    } else if (ex < -32768.0f) {
        ex = -32768.0f;
    }
    if (ey > 32767.0f) {
        ey = 32767.0f;
    } else if (ey < -32768.0f) {
        ey = -32768.0f;
    }

    const int16_t ox = (int16_t)ex;
    const int16_t oy = (int16_t)ey;
    s_pend_x -= (float)ox;
    s_pend_y -= (float)oy;

    *dx = ox;
    *dy = oy;
}
