/********************************** (C) COPYRIGHT *********************************
 * File Name          : rgb.h
 * Author             : wch-mouse
 * Version            : V1.0.0
 * Date               : 2026/09/16
 * Description        : WS2812 / SK6812 addressable RGB-LED chain on PB10 (data)
 *                     and PB2 (power/standby enable). See docs/io.csv "RGB_*".
 *
 *                     The chain carries RGB_LED_COUNT LEDs. Each LED takes 24
 *                     bits in GRB order, MSB first.
 *
 *                     Bit timing is generated entirely in hardware by TIM1
 *                     channel 1 in PWM mode feeding DMA. The CPU never enters
 *                     a critical section -- rgb_show() stops the DMA, refreshes
 *                     the CCR buffer from the colour buffer, and restarts the
 *                     DMA in loop mode. The TIM keeps producing the WS2812
 *                     waveform continuously; every loop's tail of zeros gives
 *                     the LED its >280 us reset.
 *
 *                     Wiring: TIM1 CH1 default is PA10. To drive PB10 (RGB_DIN)
 *                     enable RB_PIN_TMR1 in R16_PIN_ALTERNATE before the first
 *                     call. (PB10 is also USB FS D- / RB_PIN_USB_EN=0 leaves it
 *                     free; this board uses USB HS on PB12/PB13.)
 *********************************************************************************/
#ifndef __RGB_H__
#define __RGB_H__

#include <stdint.h>

#define RGB_LED_COUNT       2

#ifdef __cplusplus
extern "C" {
#endif

void rgb_init(void);
void rgb_show(void);

void rgb_off(void);
void rgb_set_pixel(uint8_t i, uint8_t r, uint8_t g, uint8_t b);
void rgb_set_all(uint8_t r, uint8_t g, uint8_t b);

/* Global brightness multiplier applied at fill time. Stored as 0..255; the
 * next rgb_show() scales every colour byte by (b * brightness + 127) / 255
 * before packing it into the DMA buffer, so the LED chain always latches
 * the same percentage of whatever colour you set. 0 turns the chain off;
 * 255 (default) is full brightness. */
void rgb_set_brightness(uint8_t b);

#ifdef __cplusplus
}
#endif

#endif /* __RGB_H__ */
