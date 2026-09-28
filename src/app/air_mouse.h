#ifndef __AIR_MOUSE_H__
#define __AIR_MOUSE_H__

#include <stdbool.h>
#include <stdint.h>

#include "paw3395.h"

/* Air-mouse ("waved at the screen") pointer mode.
 *
 * Holding side-1 + side-2 together for 2 s swaps the cursor's data source with
 * no power cycle: the PAW3395 optical sensor is shut down and the BMI270
 * gyroscope drives the pointer instead, so the mouse can be aimed from across
 * the room. The same combo restores the optical sensor.
 *
 * The mode is deliberately RAM-only: it resets with the mouse, so a stale flag
 * can never strand a user without their normal pointer.
 *
 * mouse.c owns the report and asks air_mouse_active() which device to read.
 * This module owns the combo detection, the sensor hand-off, the gyro
 * zero-bias calibration and the RGB/LRA feedback that announces each switch.
 */

struct bmi2_dev;

/* Baseline the optical sensor is restored to when leaving air-mouse mode.
 * paw3395_init() resets the driver defaults, so the caller's real geometry is
 * carried here and re-applied afterwards. */
typedef struct {
    struct paw3395_dev   *paw;    /* optical sensor (shutdown / re-init) */
    struct bmi2_dev      *bmi;    /* IMU that supplies the gyro          */
    uint16_t              cpi;    /* optical settings restored on exit   */
    enum paw3395_mode     mode;
    enum paw3395_lift_cut lift;
} air_mouse_cfg_t;

void air_mouse_init(const air_mouse_cfg_t *cfg);

/* Call once per scan with the debounced button bitmap. Detects the side-1 +
 * side-2 hold, performs the switch at the 2 s mark (once per hold) and
 * advances the haptic pulse timers. */
void air_mouse_poll(uint32_t now_ms, uint8_t buttons);

/* True while the gyroscope owns the cursor. */
bool air_mouse_active(void);

/* True while the entry combo is still down, so mouse.c can swallow the
 * back/forward bits instead of clicking them on the way into the mode. */
bool air_mouse_combo_held(void);

/* Gyro-derived relative motion for this scan, in the same HID-sense the packed
 * MouseReport_t expects. Valid only while air_mouse_active(); reports zero
 * while the zero-bias is still being measured. */
void air_mouse_read_motion(int16_t *dx, int16_t *dy);

#endif /* __AIR_MOUSE_H__ */
