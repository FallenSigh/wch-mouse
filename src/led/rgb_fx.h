#ifndef __RGB_FX_H__
#define __RGB_FX_H__

#include <stdbool.h>
#include <stdint.h>

/* Animation layer on top of the rgb driver. Poll it from the main loop after
 * rgb_init(): it lights the active radio mode's colour for the first second
 * after boot, shows a solid colour while air-mouse mode is active, previews the
 * mode a release would switch to while the BOOT hold is armed, and otherwise
 * runs the idle hue cycle.
 *
 *   ble           - true when this boot runs the BLE link, false for 2.4G RF
 *   switch_armed  - true while releasing the BOOT button would switch modes
 *   air_mouse     - true while the gyro owns the cursor */
/* User configuration: what the underglow shows when no transient state (the
 * boot announce, the air-mouse colour, the BOOT-hold preview) is active.
 * Defaults match the old behaviour: enabled, idle hue at brightness 1. */
#define RGB_FX_EFFECT_HUE    0u   /* idle hue cycle   */
#define RGB_FX_EFFECT_SOLID  1u   /* one fixed colour */

typedef struct {
    bool     enable;      /* false cuts the LED rail              */
    uint8_t  effect;      /* RGB_FX_EFFECT_*                      */
    uint8_t  brightness;  /* 1..255                               */
    uint8_t  r;
    uint8_t  g;
    uint8_t  b;           /* colour used by RGB_FX_EFFECT_SOLID   */
} rgb_fx_cfg_t;

void rgb_fx_set_config(const rgb_fx_cfg_t *cfg);
const rgb_fx_cfg_t *rgb_fx_get_config(void);

void rgb_fx_poll(uint32_t now_ms, bool ble, bool switch_armed, bool air_mouse);

#endif /* __RGB_FX_H__ */
