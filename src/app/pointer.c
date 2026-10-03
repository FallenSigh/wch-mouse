/*
 * src/app/pointer.c - portable relative-pointer mapper (see pointer.h).
 *
 * No hardware or platform headers: angles in degrees, dt in seconds. Ported by
 * another platform as-is; only the tuning below changes.
 */

#include "pointer.h"

#include <math.h>

/* A button press freezes the pointer; after release it stays frozen this long
 * so the finger rebound cannot move the cursor, and holding a button while
 * rotating past the unlock angle resumes motion (drag / FPS aim). */
#define POINTER_SETTLE_MS      35u
#define POINTER_UNLOCK_DEG     1.0f

/* One Euro filter: a low-pass whose cutoff rises with the estimated angular
 * speed, so it smooths hard while still and barely lags while moving. */
#define POINTER_FILTER_MIN_CUTOFF_HZ   3.0f
#define POINTER_FILTER_BETA            0.18f
#define POINTER_FILTER_DERIV_CUTOFF_HZ 2.0f

#define POINTER_TWO_PI  6.283185307179586f

static float pointer_euro_alpha(float cutoff_hz, float dt)
{
    const float tau = 1.0f / (POINTER_TWO_PI * cutoff_hz);
    return dt / (tau + dt);
}

static void pointer_euro_reset(pointer_euro_t *f, float value)
{
    f->initialized = true;
    f->last_raw    = value;
    f->filtered    = value;
    f->deriv       = 0.0f;
}

static float pointer_euro(pointer_euro_t *f, float value, float dt)
{
    if (!f->initialized) {
        pointer_euro_reset(f, value);
        return value;
    }

    const float raw_deriv = (value - f->last_raw) / dt;
    f->last_raw = value;

    const float deriv_alpha = pointer_euro_alpha(POINTER_FILTER_DERIV_CUTOFF_HZ, dt);
    f->deriv += deriv_alpha * (raw_deriv - f->deriv);

    const float cutoff = POINTER_FILTER_MIN_CUTOFF_HZ + POINTER_FILTER_BETA * fabsf(f->deriv);
    const float value_alpha = pointer_euro_alpha(cutoff, dt);
    f->filtered += value_alpha * (value - f->filtered);
    return f->filtered;
}

static void pointer_freeze(pointer_t *p, float yaw_deg, float pitch_deg)
{
    p->frozen      = true;
    p->settling    = false;
    p->pend_x      = 0.0f;
    p->pend_y      = 0.0f;
    p->start_yaw   = yaw_deg;
    p->start_pitch = pitch_deg;
}

void pointer_init(pointer_t *p, const pointer_cfg_t *cfg)
{
    p->counts_x = cfg->counts_x;
    p->counts_y = cfg->counts_y;
    p->swap     = cfg->swap;
    p->invert_x = cfg->invert_x;
    p->invert_y = cfg->invert_y;

    pointer_reset(p);
}

void pointer_reset(pointer_t *p)
{
    p->neutral_pending = false;
    p->neutral_yaw     = 0.0f;
    p->neutral_pitch   = 0.0f;

    p->angle_ready = false;
    p->last_fx     = 0.0f;
    p->last_fy     = 0.0f;
    p->pend_x      = 0.0f;
    p->pend_y      = 0.0f;

    p->fx.initialized = false;
    p->fy.initialized = false;

    p->now_ms       = 0u;
    p->raw_prev     = 0u;
    p->raw          = 0u;
    p->frozen       = false;
    p->settling     = false;
    p->settle_at    = 0u;
    p->start_yaw    = 0.0f;
    p->start_pitch  = 0.0f;
    p->clutch       = false;
    p->mid_prev     = 0u;
}

void pointer_set_sensitivity(pointer_t *p, float counts_x, float counts_y)
{
    p->counts_x = counts_x;
    p->counts_y = counts_y;
}

void pointer_rebase(pointer_t *p)
{
    p->neutral_pending = true;
}

void pointer_update(pointer_t *p,
                    float roll_deg, float yaw_deg, float pitch_deg,
                    uint8_t raw_buttons, uint32_t now_ms, float dt,
                    int16_t *dx, int16_t *dy)
{
    *dx = 0;
    *dy = 0;
    p->now_ms = now_ms;

    const uint8_t mouse_mask = (uint8_t)(POINTER_BTN_LEFT | POINTER_BTN_RIGHT);
    const uint8_t mouse      = (uint8_t)(raw_buttons & mouse_mask);

    if ((mouse != 0u) && ((p->raw_prev & mouse_mask) == 0u)) {
        pointer_freeze(p, yaw_deg, pitch_deg);
    } else if ((mouse == 0u) && ((p->raw_prev & mouse_mask) != 0u)) {
        p->settling  = true;
        p->settle_at = now_ms + POINTER_SETTLE_MS;
    }
    p->raw_prev = mouse;

    const uint8_t mid = (uint8_t)(raw_buttons & POINTER_BTN_MID);
    if ((mid != 0u) && (p->mid_prev == 0u)) {
        p->clutch = true;
        pointer_freeze(p, yaw_deg, pitch_deg);
    } else if ((mid == 0u) && (p->mid_prev != 0u) && p->clutch) {
        p->clutch          = false;
        p->frozen          = false;
        p->settling        = false;
        p->neutral_pending = true;
    }
    p->mid_prev = mid;

    if (p->frozen) {
        if (p->clutch) {
            return;   /* clutch: frozen for as long as the middle button is held */
        }

        const bool  held   = (p->raw & mouse_mask) != 0u;
        const float dyaw   = yaw_deg - p->start_yaw;
        const float dpitch = pitch_deg - p->start_pitch;

        if (held && ((dyaw * dyaw + dpitch * dpitch) >=
                     (POINTER_UNLOCK_DEG * POINTER_UNLOCK_DEG))) {
            p->frozen          = false;
            p->settling        = false;
            p->neutral_pending = true;
        } else if (!held && p->settling &&
                   ((int32_t)(now_ms - p->settle_at) >= 0)) {
            p->frozen          = false;
            p->settling        = false;
            p->neutral_pending = true;
        } else {
            return;   /* still guarded: emit no motion */
        }
    }

    if (p->neutral_pending) {
        p->neutral_yaw     = yaw_deg;
        p->neutral_pitch   = pitch_deg;
        p->neutral_pending = false;
        pointer_euro_reset(&p->fx, 0.0f);
        pointer_euro_reset(&p->fy, 0.0f);
        p->angle_ready = false;
    }

    float x = yaw_deg - p->neutral_yaw;
    float y = pitch_deg - p->neutral_pitch;

    if (p->swap) {
        const float t = x;
        x = y;
        y = t;
    }
    if (p->invert_x) {
        x = -x;
    }
    if (p->invert_y) {
        y = -y;
    }

    const float fx = pointer_euro(&p->fx, x, dt);
    const float fy = pointer_euro(&p->fy, y, dt);

    if (!p->angle_ready) {
        p->last_fx     = fx;
        p->last_fy     = fy;
        p->angle_ready = true;
        return;
    }

    p->pend_x += (fx - p->last_fx) * p->counts_x;
    p->pend_y += (fy - p->last_fy) * p->counts_y;
    p->last_fx = fx;
    p->last_fy = fy;

    float ex = p->pend_x;
    float ey = p->pend_y;

    if (ex > 32767.0f) {
        ex = 32767.0f;
    } else if (ex < -32768.0f) {
        ex = -32768.0f;
    }
    if (ey > 32767.0f) {
        ey = 32767.0f;
    } else if (ey < -32768.0f) {
        ey = -32768.0f;
    }

    const int16_t ox = (int16_t)ex;
    const int16_t oy = (int16_t)ey;
    p->pend_x -= (float)ox;
    p->pend_y -= (float)oy;

    *dx = ox;
    *dy = oy;
}
