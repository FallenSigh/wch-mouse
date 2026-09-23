/********************************** (C) COPYRIGHT *********************************
 * File Name          : motor.h
 * Author             : wch-mouse
 * Version            : V1.0.0
 * Date               : 2026/09/16
 * Description        : LRA (linear resonant actuator) haptic driver on PB0.
 *                     See docs/io.csv "MOTOR". The MCU produces a single PWM
 *                     channel; the off-board driver IC converts it into the
 *                     h-bridge drive the LRA needs.
 *
 *                     This peripheral is driven by the chip's PWMx controller
 *                     (independent of the general-purpose TIMs), with PWM6
 *                     hard-wired to PB0 -- no remap required.
 *
 *                     Frequency: LRA resonance is mechanical (typically
 *                     150-280 Hz). Override MOTOR_FREQ_HZ if this board's LRA
 *                     needs a different drive frequency.
 *********************************************************************************/
#ifndef __MOTOR_H__
#define __MOTOR_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void motor_init(void);

/* 0 = silent, 100 = full amplitude. Anything in between is the percentage
 * of the configured PWM period the LRA drive is active. Persists until the
 * next call. */
void motor_set(uint8_t pct);

/* Coast to silence (duty = 0). Most LRA driver ICs treat open as
 * "high-impedance" and let the actuator free-wheel; if this board needs a
 * hard low for brake, swap the duty=0 write inside for a GPIO reset. */
void motor_brake(void);

#ifdef __cplusplus
}
#endif

#endif /* __MOTOR_H__ */
