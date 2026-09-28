/********************************** (C) COPYRIGHT *********************************
 * File Name          : motor.c
 * Author             : wch-mouse
 * Version            : V1.0.0
 * Date               : 2026/09/16
 * Description        : ERM vibration motor switch on PB0. See docs/io.csv
 *                     "MOTOR". PB0 drives the gate of a MOSFET that switches
 *                     the motor supply, so this is a plain on/off control.
 *
 *                     The motor is a cylindrical DC (ERM) type (this board:
 *                     Z3OC1T8219731, 2.7 V rated, 2.3-3.2 V usable, start
 *                     voltage <= 2.3 V). It needs close to full supply to
 *                     start, so a chopped duty only twitches it: intensity is
 *                     on-time, not PWM amplitude.
 *********************************************************************************/
#include "motor.h"
#include "CH58x_common.h"

void motor_init(void)
{
    /* Latch low first so configuring the pin as an output cannot glitch the
     * motor on. */
    GPIOB_ResetBits(GPIO_Pin_0);
    GPIOB_ModeCfg(GPIO_Pin_0, GPIO_ModeOut_PP_5mA);
}

void motor_on(void)
{
    GPIOB_SetBits(GPIO_Pin_0);
}

void motor_off(void)
{
    GPIOB_ResetBits(GPIO_Pin_0);
}
