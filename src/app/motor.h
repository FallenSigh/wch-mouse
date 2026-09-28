/********************************** (C) COPYRIGHT *********************************
 * File Name          : motor.h
 * Author             : wch-mouse
 * Version            : V1.0.0
 * Date               : 2026/09/16
 * Description        : ERM vibration motor switch on PB0. See docs/io.csv
 *                     "MOTOR". PB0 drives the gate of a MOSFET that switches
 *                     the motor supply, so the pin is a plain on/off control,
 *                     not a PWM amplitude input.
 *
 *                     The motor is a cylindrical DC (ERM) type - on this board
 *                     a Z3OC1T8219731: 2.7 V rated, 2.3-3.2 V usable, start
 *                     voltage <= 2.3 V, ~85 mA. It needs close to full supply
 *                     to start, so a chopped duty only twitches it and the
 *                     feedback intensity must come from on-time (duration or
 *                     pulses), never from a duty cycle. Do not reintroduce a
 *                     PWM here.
 *********************************************************************************/
#ifndef __MOTOR_H__
#define __MOTOR_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Configure PB0 as the switch output, motor off. */
void motor_init(void);

/* MOSFET on: full drive. */
void motor_on(void);

/* MOSFET off: the motor free-wheels to a stop. */
void motor_off(void);

#ifdef __cplusplus
}
#endif

#endif /* __MOTOR_H__ */
