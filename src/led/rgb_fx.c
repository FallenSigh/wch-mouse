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

/* How long a mode-switch flash stays lit: the window right after boot, which is
 * what a radio-mode switch looks like since it resets into the new mode, and
 * the one an air-mouse toggle opens. */
#define RGB_FX_FLASH_MS 1000u

/* Flash brightness. 16 is the dimmest that still reads as a blink: at 1 the "on"
 * half of the blink is indistinguishable from the "off" half on a WS2812, while
 * still being cheap enough to show on battery, where the underglow is cut. */
#define RGB_FX_DIM 16u

/* The sustained air-mouse colour: brighter than the idle underglow, but not at
 * full blast. */
#define RGB_FX_BRIGHT 48u

#define RGB_FX_PERIOD_MS  33u    /* ~30 Hz refresh          */
#define RGB_FX_BLINK_MASK 0x100u /* toggles the flash ~2 Hz */

static bool s_started;
static uint32_t s_base_ms;
static uint32_t s_last_ms;
static uint16_t s_hue;

static rgb_fx_cfg_t s_cfg = {
    .enable = true,
    .transient = true,
    .effect = RGB_FX_EFFECT_HUE,
    .brightness = 1u,
    .r = 255u,
    .g = 255u,
    .b = 255u,
};

void rgb_fx_set_config(const rgb_fx_cfg_t *cfg) {
    s_cfg = *cfg;
}

const rgb_fx_cfg_t *rgb_fx_get_config(void) {
    return &s_cfg;
}

static void rgb_fx_mode_colour(bool ble) {
    if (ble) {
        rgb_set_all(0, 255, 0); /* green */
    } else {
        rgb_set_all(0, 0, 255); /* blue  */
    }
}

static void rgb_fx_air_colour(void) {
    rgb_set_all(255, 0, 255); /* magenta: gyro owns the cursor */
}

static void rgb_fx_hue(uint16_t hue) {
    uint8_t region = (uint8_t)(hue / 60u);
    uint8_t rem = (uint8_t)((hue % 60u) * 255u / 60u);
    uint8_t r;
    uint8_t g;
    uint8_t b;

    switch (region) {
    case 0:
        r = 255;
        g = rem;
        b = 0;
        break;
    case 1:
        r = 255 - rem;
        g = 255;
        b = 0;
        break;
    case 2:
        r = 0;
        g = 255;
        b = rem;
        break;
    case 3:
        r = 0;
        g = 255 - rem;
        b = 255;
        break;
    case 4:
        r = rem;
        g = 0;
        b = 255;
        break;
    default:
        r = 255;
        g = 0;
        b = 255 - rem;
        break;
    }

    rgb_set_all(r, g, b);
}

void rgb_fx_poll(uint32_t now_ms, bool ble, bool switch_armed, bool air_mouse) {
    static bool s_air_prev;
    static uint32_t s_flash_until;
    bool transient;

    if (!s_started) {
        s_started = true;
        s_air_prev = air_mouse;
        s_base_ms = now_ms;
        s_last_ms = now_ms;
    }

    /* Air-mouse is a level but the flash belongs to the edge, so remember when
     * it changed and keep the window open for a fixed time. */
    if (air_mouse != s_air_prev) {
        s_air_prev = air_mouse;
        s_flash_until = now_ms + RGB_FX_FLASH_MS;
    }

    /* Mode-switch feedback outranks the rail gate below, and runs dim: on
     * battery the underglow is cut, but the switch still has to announce
     * itself. */
    transient = s_cfg.transient && (((now_ms - s_base_ms) < RGB_FX_FLASH_MS) ||
                                    ((int32_t)(s_flash_until - now_ms) > 0) || switch_armed);

    if (!s_cfg.enable && !transient) {
        if (rgb_rail_enabled()) {
            rgb_set_enable(false);
        }
        return;
    }

    if (!rgb_rail_enabled()) {
        rgb_set_enable(true);
    }

    if (now_ms - s_last_ms < RGB_FX_PERIOD_MS) {
        return;
    }
    s_last_ms = now_ms;

    if (transient) {
        rgb_set_brightness((now_ms & RGB_FX_BLINK_MASK) ? RGB_FX_DIM : 0u);

        if (air_mouse) {
            rgb_fx_air_colour();
        } else if (switch_armed) {
            rgb_fx_mode_colour(!ble); /* the mode a release would switch to */
        } else {
            rgb_fx_mode_colour(ble);
        }
    } else if (air_mouse) {
        rgb_set_brightness(RGB_FX_BRIGHT);
        rgb_fx_air_colour();
    } else if (s_cfg.effect == RGB_FX_EFFECT_SOLID) {
        rgb_set_brightness(s_cfg.brightness);
        rgb_set_all(s_cfg.r, s_cfg.g, s_cfg.b);
    } else {
        rgb_set_brightness(s_cfg.brightness);
        rgb_fx_hue(s_hue);
        s_hue = (uint16_t)((s_hue + 1u) % 360u);
    }

    rgb_show();
}
