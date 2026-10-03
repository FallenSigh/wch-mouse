/*
 * src/app/attitude.c - portable 6-axis attitude estimator (see attitude.h).
 *
 * No hardware or platform headers: gyro in deg/s, accelerometer in g, dt in
 * seconds. Ported by another platform as-is; only the tuning below changes.
 */

#include "attitude.h"

#include <math.h>

/* Mahony proportional gain. The integral term is omitted: with no magnetometer
 * yaw is unobservable, so an integral would only pump false feedback into it. */
#define ATT_KP                1.0f

/* Accelerometer tilt-correction gate, in g^2 (0.85^2 .. 1.15^2). */
#define ATT_ACCEL_MIN_G2      0.7225f
#define ATT_ACCEL_MAX_G2      1.3225f

/* Zero-rate update: a sample counts as still below this gyro magnitude and
 * within this accelerometer tolerance of 1 g; it takes this many still samples
 * to confirm, then adapts the bias with this time constant. */
#define ATT_ZARU_GYRO_DPS     0.5f
#define ATT_ZARU_ACCEL_TOL_G  0.08f
#define ATT_ZARU_CONFIRM      100u
#define ATT_ZARU_TAU_S        4.0f

#define ATT_RAD2DEG           57.29577951308232f
#define ATT_DEG2RAD           0.017453292519943295f

static float att_clampf(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

/* Fold an angle into [-180,180] so a step across the wrap reads as the small
 * turn it really is instead of a ~360 degree jump. */
static float att_wrap180(float deg)
{
    while (deg > 180.0f) {
        deg -= 360.0f;
    }
    while (deg < -180.0f) {
        deg += 360.0f;
    }
    return deg;
}

/* At rest the accelerometer points up (opposite gravity), so on its own it
 * fixes roll and pitch; yaw is unknowable and starts at 0. */
static void att_seed_from_accel(attitude_t *a, float ax, float ay, float az)
{
    const float roll  = atan2f(ay, az);
    const float pitch = atan2f(-ax, sqrtf(ay * ay + az * az));
    const float cr = cosf(roll * 0.5f);
    const float sr = sinf(roll * 0.5f);
    const float cp = cosf(pitch * 0.5f);
    const float sp = sinf(pitch * 0.5f);

    a->q0 = cr * cp;
    a->q1 = sr * cp;
    a->q2 = cr * sp;
    a->q3 = -sr * sp;
}

static float att_raw_roll(const attitude_t *a)
{
    return atan2f(2.0f * (a->q0 * a->q1 + a->q2 * a->q3),
                  1.0f - 2.0f * (a->q1 * a->q1 + a->q2 * a->q2)) * ATT_RAD2DEG;
}

static float att_raw_pitch(const attitude_t *a)
{
    return asinf(att_clampf(2.0f * (a->q0 * a->q2 - a->q3 * a->q1), -1.0f, 1.0f))
           * ATT_RAD2DEG;
}

static float att_raw_yaw(const attitude_t *a)
{
    return atan2f(2.0f * (a->q0 * a->q3 + a->q1 * a->q2),
                  1.0f - 2.0f * (a->q2 * a->q2 + a->q3 * a->q3)) * ATT_RAD2DEG;
}

void attitude_reset(attitude_t *a)
{
    a->q0 = 1.0f;
    a->q1 = 0.0f;
    a->q2 = 0.0f;
    a->q3 = 0.0f;
    a->bias_x = 0.0f;
    a->bias_y = 0.0f;
    a->bias_z = 0.0f;

    a->zaru_count  = 0u;
    a->zaru_sum_x  = 0.0f;
    a->zaru_sum_y  = 0.0f;
    a->zaru_sum_z  = 0.0f;
    a->zaru_active = false;

    a->calib_left    = 0u;
    a->calib_samples = 0u;
    a->calib_count   = 0u;
    a->calib_gx = a->calib_gy = a->calib_gz = 0.0f;
    a->calib_ax = a->calib_ay = a->calib_az = 0.0f;
    a->ready = false;

    a->cont_ready = false;
    a->last_roll  = 0.0f;
    a->last_yaw   = 0.0f;
    a->roll_cont  = 0.0f;
    a->yaw_cont   = 0.0f;
}

void attitude_begin_calibration(attitude_t *a, uint8_t warmup, uint8_t samples)
{
    a->calib_left    = (uint8_t)(warmup + samples);
    a->calib_samples = samples;
    a->calib_count   = 0u;
    a->calib_gx = a->calib_gy = a->calib_gz = 0.0f;
    a->calib_ax = a->calib_ay = a->calib_az = 0.0f;
    a->ready = false;

    a->zaru_count  = 0u;
    a->zaru_sum_x  = 0.0f;
    a->zaru_sum_y  = 0.0f;
    a->zaru_sum_z  = 0.0f;
    a->zaru_active = false;

    a->cont_ready = false;
}

bool attitude_ready(const attitude_t *a)
{
    return a->ready;
}

/* Zero-rate update: while the device is still the residual gyro reading is
 * bias, so fold it into the estimate; a confirmed-still sample is fed as zero
 * to the filter so residual bias cannot integrate into yaw. */
static void att_zaru(attitude_t *a, float gx, float gy, float gz,
                     float ax, float ay, float az, float dt,
                     float *out_x, float *out_y, float *out_z)
{
    const float gyro_mag2  = gx * gx + gy * gy + gz * gz;
    const float accel_mag2 = ax * ax + ay * ay + az * az;

    const bool still = (gyro_mag2 <= ATT_ZARU_GYRO_DPS * ATT_ZARU_GYRO_DPS)
                    && (accel_mag2 >= 0.8464f)
                    && (accel_mag2 <= 1.1664f);

    if (!still) {
        a->zaru_count  = 0u;
        a->zaru_sum_x  = 0.0f;
        a->zaru_sum_y  = 0.0f;
        a->zaru_sum_z  = 0.0f;
        a->zaru_active = false;
        *out_x = gx;
        *out_y = gy;
        *out_z = gz;
        return;
    }

    if (!a->zaru_active) {
        a->zaru_sum_x += gx;
        a->zaru_sum_y += gy;
        a->zaru_sum_z += gz;
        if (a->zaru_count < ATT_ZARU_CONFIRM) {
            ++a->zaru_count;
        }
        if (a->zaru_count < ATT_ZARU_CONFIRM) {
            *out_x = gx;
            *out_y = gy;
            *out_z = gz;
            return;
        }

        const float inv = 1.0f / (float)a->zaru_count;
        a->bias_x += a->zaru_sum_x * inv;
        a->bias_y += a->zaru_sum_y * inv;
        a->bias_z += a->zaru_sum_z * inv;
        a->zaru_active = true;
    } else {
        const float lambda = dt / (ATT_ZARU_TAU_S + dt);
        a->bias_x += lambda * gx;
        a->bias_y += lambda * gy;
        a->bias_z += lambda * gz;
    }

    *out_x = 0.0f;
    *out_y = 0.0f;
    *out_z = 0.0f;
}

/* One Mahony step: integrate the (bias-free) gyro into the quaternion, then
 * correct the tilt with the accelerometer, used as a direction and only while
 * its magnitude is near 1 g. */
static void att_mahony(attitude_t *a, float gx, float gy, float gz,
                       float ax, float ay, float az, float dt)
{
    float wx = gx * ATT_DEG2RAD;
    float wy = gy * ATT_DEG2RAD;
    float wz = gz * ATT_DEG2RAD;

    const float a2 = ax * ax + ay * ay + az * az;

    if (a2 >= ATT_ACCEL_MIN_G2 && a2 <= ATT_ACCEL_MAX_G2) {
        const float inv = 1.0f / sqrtf(a2);
        const float nx = ax * inv;
        const float ny = ay * inv;
        const float nz = az * inv;

        const float half_vx = a->q1 * a->q3 - a->q0 * a->q2;
        const float half_vy = a->q0 * a->q1 + a->q2 * a->q3;
        const float half_vz = a->q0 * a->q0 - 0.5f + a->q3 * a->q3;

        const float half_ex = ny * half_vz - nz * half_vy;
        const float half_ey = nz * half_vx - nx * half_vz;
        const float half_ez = nx * half_vy - ny * half_vx;

        wx += 2.0f * ATT_KP * half_ex;
        wy += 2.0f * ATT_KP * half_ey;
        wz += 2.0f * ATT_KP * half_ez;
    }

    const float half_dt = 0.5f * dt;
    const float qa = a->q0;
    const float qb = a->q1;
    const float qc = a->q2;

    a->q0 += (-qb * wx - qc * wy - a->q3 * wz) * half_dt;
    a->q1 += ( qa * wx + qc * wz - a->q3 * wy) * half_dt;
    a->q2 += ( qa * wy - qb * wz + a->q3 * wx) * half_dt;
    a->q3 += ( qa * wz + qb * wy - qc * wx) * half_dt;

    const float q2 = a->q0 * a->q0 + a->q1 * a->q1 + a->q2 * a->q2 + a->q3 * a->q3;
    if (q2 > 0.000001f) {
        const float inv = 1.0f / sqrtf(q2);
        a->q0 *= inv;
        a->q1 *= inv;
        a->q2 *= inv;
        a->q3 *= inv;
    }
}

static void att_update_continuous(attitude_t *a)
{
    const float roll = att_raw_roll(a);
    const float yaw  = att_raw_yaw(a);

    if (!a->cont_ready) {
        a->roll_cont  = roll;
        a->yaw_cont   = yaw;
        a->cont_ready = true;
    } else {
        a->roll_cont += att_wrap180(roll - a->last_roll);
        a->yaw_cont  += att_wrap180(yaw - a->last_yaw);
    }

    a->last_roll = roll;
    a->last_yaw  = yaw;
}

bool attitude_update(attitude_t *a, float gx, float gy, float gz,
                     float ax, float ay, float az, float dt)
{
    if (a->calib_left > 0u) {
        if (a->calib_left <= a->calib_samples) {
            a->calib_gx += gx;
            a->calib_gy += gy;
            a->calib_gz += gz;
            a->calib_ax += ax;
            a->calib_ay += ay;
            a->calib_az += az;
            a->calib_count++;
        }

        if (--a->calib_left == 0u) {
            const float inv = 1.0f / (float)a->calib_count;

            a->bias_x = a->calib_gx * inv;
            a->bias_y = a->calib_gy * inv;
            a->bias_z = a->calib_gz * inv;
            att_seed_from_accel(a, a->calib_ax, a->calib_ay, a->calib_az);
            a->ready = true;
        }
        return false;
    }

    float zgx;
    float zgy;
    float zgz;

    att_zaru(a, gx - a->bias_x, gy - a->bias_y, gz - a->bias_z,
             ax, ay, az, dt, &zgx, &zgy, &zgz);
    att_mahony(a, zgx, zgy, zgz, ax, ay, az, dt);
    att_update_continuous(a);
    return true;
}

float attitude_roll_deg(const attitude_t *a)
{
    return a->roll_cont;
}

float attitude_pitch_deg(const attitude_t *a)
{
    return att_raw_pitch(a);
}

float attitude_yaw_deg(const attitude_t *a)
{
    return a->yaw_cont;
}

void attitude_get_bias(const attitude_t *a, float *bx, float *by, float *bz)
{
    *bx = a->bias_x;
    *by = a->bias_y;
    *bz = a->bias_z;
}
