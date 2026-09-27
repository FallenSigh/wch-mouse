#ifndef __RGB_FX_H__
#define __RGB_FX_H__

#include <stdbool.h>
#include <stdint.h>

/* Animation layer on top of the rgb driver. Poll it from the main loop after
 * rgb_init(): it lights the active radio mode's colour for the first second
 * after boot, previews the mode a release would switch to while the BOOT hold
 * is armed, and otherwise runs the idle hue cycle.
 *
 *   ble           - true when this boot runs the BLE link, false for 2.4G RF
 *   switch_armed  - true while releasing the BOOT button would switch modes */
void rgb_fx_poll(uint32_t now_ms, bool ble, bool switch_armed);

#endif /* __RGB_FX_H__ */
