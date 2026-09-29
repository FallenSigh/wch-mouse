#include "mouse.h"
#include "CH58x_common.h"
#include "air_mouse.h"
#include "transport.h"
#include "paw3395.h"
#include "paw3395_port.h"
#include "log.h"
#include "power.h"

#define BTN_LEFT_PIN      GPIO_Pin_7     // PB7   PR_SW1
#define BTN_RIGHT_PIN     GPIO_Pin_1     // PB1   PR_SW2
#define BTN_MID_PIN       GPIO_Pin_9     // PB9   PR_SW3
#define BTN_SIDE1_PIN     GPIO_Pin_11    // PB11  PR_SW4 (Back)
#define BTN_SIDE2_PIN     GPIO_Pin_4     // PB4   PR_SW5 (Forward)
#define ENC_A_PIN         GPIO_Pin_8     // PB8   ENC_A
#define ENC_B_PIN         GPIO_Pin_19    // PB19  ENC_B

#define INPUT_ALL_PINS    (BTN_LEFT_PIN | BTN_RIGHT_PIN | BTN_MID_PIN \
                           | BTN_SIDE1_PIN | BTN_SIDE2_PIN | ENC_A_PIN | ENC_B_PIN)

static volatile uint8_t s_btn_stable;
static volatile uint8_t s_btn_raw_prev;
static volatile uint32_t s_btn_raw_ms;

static struct paw3395_dev *s_sensor;

static uint32_t s_stat_ms;
static uint32_t s_pub_cnt;
static uint32_t s_scan_cnt;

/* A publish the transport rejects (the USB endpoint is still busy) carries
 * relative motion, so it is held here and folded into the next report. At a
 * high report rate the endpoint is busy often enough that dropping these would
 * visibly starve the cursor. */
static int32_t s_pend_dx;
static int32_t s_pend_dy;
static int32_t s_pend_wheel;

/* Debounce in milliseconds, so it does not scale with the report rate. */
#define DEBOUNCE_MS   3u

static int16_t clamp16(int32_t v)
{
    if (v > 32767) {
        return 32767;
    }
    if (v < -32768) {
        return -32768;
    }
    return (int16_t)v;
}

static int8_t clamp8(int32_t v)
{
    if (v > 127) {
        return 127;
    }
    if (v < -128) {
        return -128;
    }
    return (int8_t)v;
}

/* ===== scroll wheel: 2-bit Gray-code quadrature (PB8=A, PB19=B) =====
 *
 * Contacts pull to GND (inputs are pulled up). The transition table is
 * indexed by (prev<<2)|curr; it yields -1/0/+1 per valid single-bit
 * transition and 0 for anything else, so contact bounce and double-edges
 * are discarded. One mechanical detent is one full cycle = 4 transitions.
 */
#define ENC_TRANSITIONS_PER_DETENT   2
#define ENC_WHEEL_SIGN              (+1)   /* flip to -1 if scroll direction is inverted */

static int8_t  s_enc_accum;
static uint8_t s_enc_prev;

static int8_t encoder_update(uint32_t pins)
{
    static const int8_t quad[16] = {
        0, -1,  1,  0,
        1,  0,  0, -1,
       -1,  0,  0,  1,
        0,  1, -1,  0
    };

    const uint8_t a = (pins & ENC_A_PIN) ? 1u : 0u;
    const uint8_t b = (pins & ENC_B_PIN) ? 1u : 0u;
    const uint8_t state = (uint8_t)((a << 1) | b);

    s_enc_accum = (int8_t)(s_enc_accum + quad[(s_enc_prev << 2) | state]);
    s_enc_prev  = state;

    int8_t steps = 0;
    while (s_enc_accum >= ENC_TRANSITIONS_PER_DETENT) {
        s_enc_accum = (int8_t)(s_enc_accum - ENC_TRANSITIONS_PER_DETENT);
        steps++;
    }
    while (s_enc_accum <= -ENC_TRANSITIONS_PER_DETENT) {
        s_enc_accum = (int8_t)(s_enc_accum + ENC_TRANSITIONS_PER_DETENT);
        steps--;
    }
    return (int8_t)(steps * ENC_WHEEL_SIGN);
}

void mouse_init(struct paw3395_dev *sensor)
{
    s_sensor = sensor;

    GPIOB_ModeCfg(INPUT_ALL_PINS, GPIO_ModeIN_PU);

    s_btn_stable   = HID_BTN_MASK;
    s_btn_raw_prev = HID_BTN_MASK;
    s_btn_raw_ms   = 0u;

    s_pend_dx    = 0;
    s_pend_dy    = 0;
    s_pend_wheel = 0;

    const uint32_t pins = GPIOB_ReadPort();

    s_enc_prev  = (uint8_t)(((pins & ENC_A_PIN) ? 2u : 0u) | ((pins & ENC_B_PIN) ? 1u : 0u));
    s_enc_accum = 0;

    LOG_I("MOUSE", "L=PB7 R=PB1 M=PB9 S1=PB11 S2=PB4 ENC_A=PB8 ENC_B=PB19");
}

void mouse_scan(uint32_t now_ms)
{
    /* The caller gates the cadence (the configured report rate), so every call
     * here is a scan. */
    s_scan_cnt++;
    const uint32_t pins = GPIOB_ReadPort();

    uint8_t raw_btn = 0;
    uint8_t active  = 0;
    if (!(pins & BTN_LEFT_PIN))   raw_btn |= HID_BTN_LEFT;
    if (!(pins & BTN_RIGHT_PIN))  raw_btn |= HID_BTN_RIGHT;
    if (!(pins & BTN_MID_PIN))    raw_btn |= HID_BTN_MID;
    if (!(pins & BTN_SIDE1_PIN))  raw_btn |= HID_BTN_BACK;
    if (!(pins & BTN_SIDE2_PIN))  raw_btn |= HID_BTN_FWD;

    if (raw_btn != s_btn_raw_prev) {
        s_btn_raw_prev = raw_btn;
        s_btn_raw_ms   = now_ms;
    } else if (s_btn_stable != raw_btn && (now_ms - s_btn_raw_ms) >= DEBOUNCE_MS) {
        s_btn_stable = raw_btn;
        active       = 1;
    }

    uint8_t buttons = s_btn_stable;

    /* Air-mouse mode owns the side-1 + side-2 combo it toggles on, so swallow
     * those bits while it is held rather than clicking back/forward. */
    air_mouse_poll(now_ms, buttons);
    if (air_mouse_combo_held()) {
        buttons &= (uint8_t)~(HID_BTN_BACK | HID_BTN_FWD);
    }

    int16_t dx = 0;
    int16_t dy = 0;

    if (air_mouse_active()) {
        /* Gyro drives the cursor (already HID-sense). */
        air_mouse_read_motion(&dx, &dy);
    } else {
        int16_t mx;
        int16_t my;

        paw3395_motion_read(&mx, &my);
        dx = (int16_t)-mx;
        dy = (int16_t)-my;
    }

    int8_t wheel = encoder_update(pins);

    if (dx != 0 || dy != 0 || wheel != 0) {
        active = 1;
    }

    if (active != 0u) {
        power_note_activity(now_ms);
    }

    /* Send while the host is out of date: the buttons differ from what it last
     * saw, or there is motion/wheel left to deliver. */
    const int32_t dx_sum = (int32_t)dx + s_pend_dx;
    const int32_t dy_sum = (int32_t)dy + s_pend_dy;
    const int32_t wh_sum = (int32_t)wheel + s_pend_wheel;
    const int16_t dx_out = clamp16(dx_sum);
    const int16_t dy_out = clamp16(dy_sum);
    const int8_t  wh_out = clamp8(wh_sum);

    /* Report every period rather than only on change: the polling rate is set
     * by our report clock and the sensor delivers new deltas at only ~1.5 kHz,
     * so reporting on change caps the measured rate at the sensor's rate. */
    MouseReport_t rpt = {
        .buttons = buttons,
        .dx      = dx_out,
        .dy      = dy_out,
        .wheel   = wh_out
    };

    s_pub_cnt++;

    /* A rejected publish sent nothing, so the whole sum stays pending; a
     * clamped one keeps its excess. */
    if (transport_router_publish(&rpt)) {
        s_pend_dx    = dx_sum - dx_out;
        s_pend_dy    = dy_sum - dy_out;
        s_pend_wheel = wh_sum - wh_out;
    } else {
        s_pend_dx    = dx_sum;
        s_pend_dy    = dy_sum;
        s_pend_wheel = wh_sum;
    }

    if (now_ms - s_stat_ms >= 5000u) {
        s_stat_ms = now_ms;
        LOG_I("MOUSE", "pub %u scan %u in 5s", (unsigned)s_pub_cnt, (unsigned)s_scan_cnt);
        s_pub_cnt = 0;
        s_scan_cnt = 0;
    }
}
