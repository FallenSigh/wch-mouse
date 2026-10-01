#ifndef __RGB_FX_H__
#define __RGB_FX_H__

#include <stdbool.h>
#include <stdint.h>

/* Animation layer on top of the rgb driver. Poll it from the main loop after
 * rgb_init(). Mode switches - the boot announce, an air-mouse toggle, and the
 * armed switch combo - flash the mode colour dimly and show even when the
 * underglow is cut for battery; otherwise it runs the idle hue cycle, or the
 * sustained air-mouse colour.
 *
 *   ble           - true when this boot runs the BLE link, false for 2.4G RF
 *   switch_armed  - true while releasing the switch combo would change modes
 *   air_mouse     - true while the gyro owns the cursor */
/* User configuration: what the underglow shows when no transient state is
 * active. Defaults match the old behaviour: enabled, idle hue at brightness 1. */
#define RGB_FX_EFFECT_HUE    0u   /* idle hue cycle   */
#define RGB_FX_EFFECT_SOLID  1u   /* one fixed colour */

typedef struct {
    bool     enable;      /* false cuts the LED rail                */
    bool     transient;   /* false suppresses the mode-switch flash */
    uint8_t  effect;      /* RGB_FX_EFFECT_*                        */
    uint8_t  brightness;  /* 1..255                               */
    uint8_t  r;
    uint8_t  g;
    uint8_t  b;           /* colour used by RGB_FX_EFFECT_SOLID   */
} rgb_fx_cfg_t;

void rgb_fx_set_config(const rgb_fx_cfg_t *cfg);
const rgb_fx_cfg_t *rgb_fx_get_config(void);

void rgb_fx_poll(uint32_t now_ms, bool ble, bool switch_armed, bool air_mouse);

#endif /* __RGB_FX_H__ */
