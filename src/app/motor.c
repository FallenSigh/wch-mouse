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

/* Counted down rather than compared against a deadline, so starting a pulse
 * needs no clock: the config channel runs in an ISR and could not supply one. */
static uint32_t s_left_ms;   /* 0 while idle */
static uint32_t s_last_ms;   /* poll time of the last decrement */
static bool     s_running;
static bool     s_enabled = true;

void motor_set_enable(bool on)
{
    s_enabled = on;
    if (!on) {
        motor_off();
    }
}

bool motor_enabled(void)
{
    return s_enabled;
}

void motor_on(void)
{
    if (!s_enabled) {
        return;
    }
    GPIOB_SetBits(GPIO_Pin_0);
}

void motor_off(void)
{
    s_running  = false;
    s_left_ms  = 0u;
    GPIOB_ResetBits(GPIO_Pin_0);
}

void motor_pulse(uint32_t ms)
{
    if (!s_enabled || ms == 0u) {
        motor_off();
        return;
    }

    motor_on();
    s_running = true;
    s_left_ms = ms;
    s_last_ms = 0u;   /* the next poll latches the clock */
}

void motor_poll(uint32_t now_ms)
{
    uint32_t elapsed;

    if (!s_running) {
        return;
    }

    if (s_last_ms == 0u) {
        s_last_ms = now_ms;
        return;
    }

    elapsed   = now_ms - s_last_ms;
    s_last_ms = now_ms;

    if (elapsed >= s_left_ms) {
        motor_off();
        return;
    }

    s_left_ms -= elapsed;
}

bool motor_active(void)
{
    return s_running;
}
