/*
 * src/led/rgb_fx.c - animation layer over the WS2812 driver (rgb.c).
 *
 * Owns the things the underglow can show: the idle hue cycle, the boot
 * announcement of the active radio mode, the solid air-mouse colour and the
 * preview blink while the BOOT hold is armed. The driver does all the bit
 * timing in hardware, so this only edits the colour buffer and triggers a
 * one-frame DMA refresh.
 */

#include "rgb_fx.h"

#include "rgb.h"

/* Solid mode colour for this long after boot, so a power-up says which radio
 * it is running without anyone having to read a log. */
#define RGB_FX_ANNOUNCE_MS   1000u

/* Brighter than the 1/255 idle underglow, but not at full blast. */
#define RGB_FX_BRIGHT        48u

#define RGB_FX_PERIOD_MS     33u    /* ~30 Hz refresh            */
#define RGB_FX_BLINK_MASK    0x100u /* toggles the preview ~2 Hz */

static bool     s_started;
static uint32_t s_base_ms;
static uint32_t s_last_ms;
static uint16_t s_hue;

static void rgb_fx_mode_colour(bool ble)
{
    if (ble) {
        rgb_set_all(0, 255, 0);     /* green */
    } else {
        rgb_set_all(0, 0, 255);     /* blue  */
    }
}

static void rgb_fx_air_colour(void)
{
    rgb_set_all(255, 0, 255);       /* magenta: gyro owns the cursor */
}

static void rgb_fx_hue(uint16_t hue)
{
    uint8_t region = (uint8_t)(hue / 60u);
    uint8_t rem    = (uint8_t)((hue % 60u) * 255u / 60u);
    uint8_t r;
    uint8_t g;
    uint8_t b;

    switch (region) {
        case 0:  r = 255;       g = rem;       b = 0;        break;
        case 1:  r = 255 - rem; g = 255;       b = 0;        break;
        case 2:  r = 0;         g = 255;       b = rem;      break;
        case 3:  r = 0;         g = 255 - rem; b = 255;      break;
        case 4:  r = rem;       g = 0;         b = 255;      break;
        default: r = 255;       g = 0;         b = 255 - rem; break;
    }

    rgb_set_all(r, g, b);
}

void rgb_fx_poll(uint32_t now_ms, bool ble, bool switch_armed, bool air_mouse)
{
    if (!s_started) {
        s_started = true;
        s_base_ms = now_ms;
        s_last_ms = now_ms;
    }

    if (now_ms - s_last_ms < RGB_FX_PERIOD_MS) {
        return;
    }
    s_last_ms = now_ms;

    if (now_ms - s_base_ms < RGB_FX_ANNOUNCE_MS) {
        rgb_set_brightness(RGB_FX_BRIGHT);
        rgb_fx_mode_colour(ble);
    } else if (air_mouse) {
        rgb_set_brightness(RGB_FX_BRIGHT);
        rgb_fx_air_colour();
    } else if (switch_armed) {
        rgb_set_brightness((now_ms & RGB_FX_BLINK_MASK) ? RGB_FX_BRIGHT
                                                        : (RGB_FX_BRIGHT / 8u));
        rgb_fx_mode_colour(!ble);
    } else {
        rgb_set_brightness(1);
        rgb_fx_hue(s_hue);
        s_hue = (uint16_t)((s_hue + 1u) % 360u);
    }

    rgb_show();
}
