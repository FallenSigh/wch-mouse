/********************************** (C) COPYRIGHT *********************************
 * File Name          : motor.c
 * Author             : wch-mouse
 * Version            : V1.0.0
 * Date               : 2026/09/16
 * Description        : LRA driver on PB0 using the chip's PWMx controller
 *                     (PWM6 channel). PWM6 is hard-wired to PB0 -- no remap.
 *
 *                     We run the PWMx controller in 16-bit mode because the
 *                     required cycle for ~200 Hz at Fsys = 62.4 MHz does not
 *                     fit in the 8-bit cycle register (max 255).
 *
 *                     Frequency:
 *                       Fpwm  = Fsys / ((CLKDIV + 1) * (CYCLE + 1))
 *                     With CLKDIV = 8 and Fsys = 62.4 MHz the cycle register
 *                     for 200 Hz is 62400000 / 8 / 200 - 1 = 38999.
 *                     Override MOTOR_FREQ_HZ for the specific LRA on this board.
 *********************************************************************************/
#include "motor.h"
#include "CH58x_common.h"

#ifndef MOTOR_FREQ_HZ
#define MOTOR_FREQ_HZ     200U    /* typical LRA resonance */
#endif

#define MOTOR_CLK_DIV     8U
/* At runtime Fsys is read once from the configured SYSCLK_FREQ. To avoid
 * dragging that constant in here we use the conservative 62.4 MHz figure
 * matching our build (CLK_SOURCE_HSE_PLL_62_4MHz). If the project switches
 * system clock, recompute. */
#define MOTOR_FSYS_HZ     62400000U

#define MOTOR_CYCLE   ((uint16_t)((MOTOR_FSYS_HZ / (MOTOR_CLK_DIV + 1U) / MOTOR_FREQ_HZ) - 1U))

void motor_init(void)
{
    /* Let the PWMx controller drive PB0. */
    GPIOB_ModeCfg(GPIO_Pin_0, GPIO_ModeOut_PP_5mA);

    /* Set up the PWMx controller: clock divisor and 16-bit cycle.
     * Period in ticks = (CLKDIV + 1) * (CYCLE + 1). */
    PWMX_CLKCfg(MOTOR_CLK_DIV);
    PWMX_16bit_CycleCfg(MOTOR_CYCLE);

    /* Start at 0% duty so the LRA stays still until the user asks. */
    PWMX_16bit_ACTOUT(CH_PWM6, 0, High_Level, ENABLE);
}

void motor_set(uint8_t pct)
{
    if (pct > 100) pct = 100;
    /* Scale 0..100 % into 0..CYCLE. Integer truncation is fine; the LRA
     * is a mechanical device that can't resolve sub-percent steps. */
    uint16_t duty = (uint16_t)((uint32_t)pct * MOTOR_CYCLE / 100U);
    PWMX_16bit_ACTOUT(CH_PWM6, duty, High_Level, ENABLE);
}

void motor_brake(void)
{
    PWMX_16bit_ACTOUT(CH_PWM6, 0, High_Level, ENABLE);
}
