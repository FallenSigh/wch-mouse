/********************************** (C) COPYRIGHT *********************************
 * File Name          : rgb.c
 * Author             : wch-mouse
 * Version            : V1.0.0
 * Date               : 2026/09/16
 * Description        : WS2812 / SK6812 driver on PB10 using TIM1 CH1 PWM + DMA.
 *                     The CPU only edits the colour buffer and triggers a
 *                     DMA refresh in rgb_show(); the WS2812 bit timing is
 *                     produced entirely in hardware. No global IRQ disable.
 *
 *                     Timing (Fsys = 62.4 MHz, 1 cycle = 16 ns):
 *                       period      = 100 cycles ~ 1.60 us
 *                       CCR "0"     =  22 cycles ~ 352 ns (T0H)
 *                       CCR "1"     =  53 cycles ~ 848 ns (T1H)
 *                       reset tail  = 200 cycles ~ 320 us (> WS2812's 280 us
 *                                                     latch threshold)
 *                     T0H  352 ns     (in 220..380 ns)
 *                     T0L 1248 ns     (in 580..1000 ns -- well above min)
 *                     T1H  848 ns     (in 580..1000 ns)
 *                     T1L  752 ns     (in 580..1000 ns)
 *
 *                     To re-tune for a different Fsys, scale the constants
 *                     linearly with cycles = round(target_ns / 16).
 *********************************************************************************/
#include "rgb.h"
#include "CH58x_common.h"

#include <stddef.h>
#include <string.h>

#define RGB_DIN_PIN     GPIO_Pin_10   /* PB10 = TIM1_CH1_ after remap       */
#define RGB_EN_PIN      GPIO_Pin_2    /* PB2  power/standby                 */

#define RGB_PERIOD           100U    /* T_period ~ 1.60 us                  */
#define RGB_CCR_0             22U    /* T0H ~ 352 ns                        */
#define RGB_CCR_1             53U    /* T1H ~ 848 ns                        */
#define RGB_RESET_PERIODS    200U    /* > 280 us reset                      */

#define RGB_BITS_PER_LED       24U
#define RGB_BITBUF_LEN  (RGB_LED_COUNT * RGB_BITS_PER_LED + RGB_RESET_PERIODS)
#define RGB_FRAME_BYTES (RGB_LED_COUNT * RGB_BITS_PER_LED * 4U)   /* bit words, no tail */

/* CCR is written 32-bit wide by the DMA into the TIM1 FIFO. Only the low
 * bits are interpreted as the active width; the rest are ignored. We pack
 * the actual CCR in the low byte for clarity. */
static __attribute__((aligned(4))) uint32_t s_bitbuf[RGB_BITBUF_LEN];
static uint8_t  s_color[RGB_LED_COUNT * 3];   /* GRB, one byte per channel */
static uint8_t  s_brightness = 255;          /* 0..255, applied at fill time */

/* Scale an 8-bit colour byte by the current brightness, with rounding so
 * 255 stays 255 instead of dropping to 254 after /255. */
static inline uint8_t apply_brightness(uint8_t v)
{
    return (uint8_t)((((uint16_t)v * s_brightness) + 127U) / 255U);
}

static void fill_bitbuf(void)
{
    uint32_t *bp = s_bitbuf;

    for (uint8_t i = 0; i < RGB_LED_COUNT; i++) {
        uint32_t word = ((uint32_t)apply_brightness(s_color[i * 3 + 0]) << 16)   /* G */
                      | ((uint32_t)apply_brightness(s_color[i * 3 + 1]) << 8)    /* R */
                      |  (uint32_t)apply_brightness(s_color[i * 3 + 2]);         /* B */
        for (uint8_t bit = 0; bit < 24; bit++) {
            *bp++ = (word & 0x800000U) ? RGB_CCR_1 : RGB_CCR_0;
            word <<= 1;
        }
    }
    /* WS2812 latch: every period here is CCR=0, so PB10 stays low; the tail
     * is long enough (>= RGB_RESET_PERIODS * RGB_PERIOD) to satisfy the
     * >280 us reset. */
    for (uint16_t i = 0; i < RGB_RESET_PERIODS; i++) *bp++ = 0;
}

void rgb_init(void)
{
    /* Remap TIM1 CH1 to PB10. RB_PIN_TMR1 default is 0, so PA10 would
     * be the timer pin -- explicitly enable the alternate. */
    if (!(R16_PIN_ALTERNATE & RB_PIN_TMR1)) {
        GPIOPinRemap(ENABLE, RB_PIN_TMR1);
    }

    /* Configure the data pin for the timer to drive; enable the rail. */
    GPIOB_ModeCfg(RGB_DIN_PIN, GPIO_ModeOut_PP_5mA);
    GPIOB_ModeCfg(RGB_EN_PIN,  GPIO_ModeOut_PP_5mA);
    rgb_set_enable(true);

    memset(s_color, 0, sizeof(s_color));
    s_brightness = 255;
    fill_bitbuf();

    /* TIM1 PWM: high-while-CNT<CCR (active high), 1 pulse per period.
     * CNT_END = period. Initial CCR = 0 so the line idles low until the DMA
     * starts feeding it. */
    TMR1_PWMInit(High_Level, PWM_Times_1);
    TMR1_PWMCycleCfg(RGB_PERIOD);
    TMR1_PWMActDataWidth(0);

    /* DMA in loop mode: each update event pops one uint32 from s_bitbuf
     * into the TIM1 FIFO, which becomes the next CCR. The DMA restarts
     * from BEG every time the buffer is exhausted, so the LED chain
     * keeps latching the current frame until rgb_show() reloads it. */
    TMR1_DMACfg(ENABLE,
                (uint32_t)s_bitbuf,
                (uint32_t)(s_bitbuf + RGB_BITBUF_LEN),
                Mode_LOOP);

    TMR1_PWMEnable();
    TMR1_Enable();
}

void rgb_off(void)
{
    memset(s_color, 0, sizeof(s_color));
}

void rgb_set_enable(bool on)
{
    if (on) {
        GPIOB_SetBits(RGB_EN_PIN);
    } else {
        GPIOB_ResetBits(RGB_EN_PIN);
    }
}

void rgb_set_brightness(uint8_t b)
{
    /* Clamp at the upper bound; 0 is a valid "off" multiplier. */
    s_brightness = b;
}

void rgb_set_pixel(uint8_t i, uint8_t r, uint8_t g, uint8_t b)
{
    if (i >= RGB_LED_COUNT) return;
    /* WS2812 takes GRB, MSB-first per colour. */
    s_color[i * 3 + 0] = g;
    s_color[i * 3 + 1] = r;
    s_color[i * 3 + 2] = b;
}

void rgb_set_all(uint8_t r, uint8_t g, uint8_t b)
{
    for (uint8_t i = 0; i < RGB_LED_COUNT; i++) rgb_set_pixel(i, r, g, b);
}

/* Set by rgb_show(), consumed by rgb_poll(). */
static volatile bool s_dirty;

void rgb_show(void)
{
    /* Only record the request: rgb_poll() does the work. Stopping the DMA here
     * would freeze the last CCR wherever the frame happened to be, and a CCR
     * left on a "1" bit keeps the TIM re-emitting it - the chain loses bit
     * alignment and the LEDs after the first one latch garbage. */
    s_dirty = true;
}

void rgb_poll(uint32_t now_ms)
{
    uint32_t base;
    uint32_t now;

    (void)now_ms;

    if (!s_dirty) {
        return;
    }

    /* Rewrite the frame only while the DMA is walking the reset tail - past
     * the last LED's bits, before the loop wraps - so the panel is never
     * clocked a half-updated frame and the DMA is never stopped. The DMA
     * registers carry the RAM offset rather than the full address. */
    base = (uint32_t)(uintptr_t)s_bitbuf & 0x1FFFFu;
    now  = R32_TMR1_DMA_NOW & 0x1FFFFu;

    if (((now - base) & 0x1FFFFu) >= RGB_FRAME_BYTES) {
        fill_bitbuf();
        s_dirty = false;
    }
}
