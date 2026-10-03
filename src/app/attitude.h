#ifndef __ATTITUDE_H__
#define __ATTITUDE_H__

#include <stdbool.h>
#include <stdint.h>

/* Portable 6-axis attitude estimator.
 *
 * A Mahony quaternion is driven by the gyro and corrected on tilt by the
 * accelerometer, with an entry bias calibration and an online zero-rate update
 * (ZARU). It has no hardware or platform dependencies beyond <math.h>, so it
 * drops into any project that can supply gyro + accelerometer samples.
 *
 * Units: gyro in deg/s, accelerometer in g, dt in seconds. Roll and yaw are
 * continuous (unwrapped) so a turn through +-180 keeps counting; pitch is the
 * instantaneous value.
 *
 * The owning module holds one instance and re-seeds it with a new calibration
 * whenever the sensor is (re)started. */

typedef struct {
    float q0, q1, q2, q3;             /* body -> reference, scalar first */
    float bias_x, bias_y, bias_z;     /* gyro bias, deg/s */

    /* zero-rate update */
    uint16_t zaru_count;
    float    zaru_sum_x, zaru_sum_y, zaru_sum_z;
    bool     zaru_active;

    /* entry calibration */
    uint8_t  calib_left;              /* warmup + samples still to collect */
    uint8_t  calib_samples;           /* how many of those are averaged */
    uint16_t calib_count;             /* samples actually averaged */
    float    calib_gx, calib_gy, calib_gz;
    float    calib_ax, calib_ay, calib_az;
    bool     ready;

    /* continuous euler */
    bool  cont_ready;
    float last_roll, last_yaw;
    float roll_cont, yaw_cont;
} attitude_t;

void attitude_reset(attitude_t *a);

/* Start an entry calibration: discard `warmup` samples, then average `samples`
 * into the gyro bias and seed the tilt from the accelerometer. Until it
 * finishes, attitude_update() reports false and no angles are valid. */
void attitude_begin_calibration(attitude_t *a, uint8_t warmup, uint8_t samples);

/* True once a calibration has produced a usable attitude. */
bool attitude_ready(const attitude_t *a);

/* Feed one sample (gyro deg/s, accelerometer g, dt seconds). Returns true when
 * the attitude is valid, false while the entry calibration is still running. */
bool attitude_update(attitude_t *a, float gx, float gy, float gz,
                     float ax, float ay, float az, float dt);

/* Continuous (unwrapped) roll and yaw, instantaneous pitch, in degrees. */
float attitude_roll_deg(const attitude_t *a);
float attitude_pitch_deg(const attitude_t *a);
float attitude_yaw_deg(const attitude_t *a);

/* Current gyro bias estimate, deg/s. */
void attitude_get_bias(const attitude_t *a, float *bx, float *by, float *bz);

#endif /* __ATTITUDE_H__ */
