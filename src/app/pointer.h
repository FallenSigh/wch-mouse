#ifndef __POINTER_H__
#define __POINTER_H__

#include <stdbool.h>
#include <stdint.h>

/* Portable relative-pointer mapper.
 *
 * Turns gravity-referenced angles into relative mouse counts, with a neutral
 * pose, a per-axis One Euro filter, an angle-difference output and a click
 * guard / clutch. It has no hardware or platform dependencies, so it drops into
 * any project whose input device reports the angles that attitude.c produces.
 *
 * The input angles are the continuous (unwrapped) roll and yaw plus the
 * instantaneous pitch, in degrees. The output is the same HID-sense relative
 * motion a normal mouse report carries.
 *
 * Button bits mirror the HID button report so the caller can pass its mask
 * straight through. */

#define POINTER_BTN_LEFT   0x01u
#define POINTER_BTN_RIGHT  0x02u
#define POINTER_BTN_MID    0x04u

typedef struct {
    float   counts_x;   /* mouse counts per degree of rotation */
    float   counts_y;
    uint8_t swap;       /* exchange the X/Y angles            */
    uint8_t invert_x;   /* reverse each axis                  */
    uint8_t invert_y;
} pointer_cfg_t;

/* One Euro filter state (one per axis). */
typedef struct {
    bool  initialized;
    float last_raw;
    float filtered;
    float deriv;
} pointer_euro_t;

typedef struct {
    float counts_x, counts_y;
    uint8_t swap, invert_x, invert_y;

    bool  neutral_pending;
    float neutral_yaw, neutral_pitch;

    bool  angle_ready;
    float last_fx, last_fy;
    float pend_x, pend_y;

    pointer_euro_t fx, fy;

    uint32_t now_ms;
    uint8_t  raw_prev;
    uint8_t  raw;
    bool     frozen, settling;
    uint32_t settle_at;
    float    start_yaw, start_pitch;
    bool     clutch;
    uint8_t  mid_prev;
} pointer_t;

void pointer_init(pointer_t *p, const pointer_cfg_t *cfg);

/* Clear the runtime state (neutral, filters, guard) but keep the configuration.
 * Call on mode entry. */
void pointer_reset(pointer_t *p);

/* Runtime sensitivity, counts per degree. */
void pointer_set_sensitivity(pointer_t *p, float counts_x, float counts_y);

/* Request a neutral capture on the next pointer_update(). */
void pointer_rebase(pointer_t *p);

/* Feed one attitude sample. `raw_buttons` is the undebounced mask (so the guard
 * freezes on the press edge). Writes the relative counts for this sample, which
 * are zero while the guard or the entry re-base is in effect. */
void pointer_update(pointer_t *p,
                    float roll_deg, float yaw_deg, float pitch_deg,
                    uint8_t raw_buttons, uint32_t now_ms, float dt,
                    int16_t *dx, int16_t *dy);

#endif /* __POINTER_H__ */
