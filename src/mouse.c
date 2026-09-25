#include "mouse.h"
#include "CH58x_common.h"
#include "transport.h"
#include "paw3395.h"
#include "log.h"

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
static volatile uint8_t s_btn_history;
static volatile uint8_t s_btn_match;

static uint32_t s_stat_ms;
static uint32_t s_scan_cnt;
static uint32_t s_pub_cnt;

#define DEBOUNCE_SAMPLES   3

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

void mouse_init() {
    GPIOB_ModeCfg(INPUT_ALL_PINS, GPIO_ModeIN_PU);

    s_btn_stable  = HID_BTN_MASK;
    s_btn_history = HID_BTN_MASK;
    s_btn_match   = 0;

    uint32_t pins = GPIOB_ReadPort();
    s_enc_prev  = (uint8_t)(((pins & ENC_A_PIN) ? 2u : 0u) | ((pins & ENC_B_PIN) ? 1u : 0u));
    s_enc_accum = 0;

    LOG_I("MOUSE", "L=PB7 R=PB1 M=PB9 S1=PB11 S2=PB4 ENC_A=PB8 ENC_B=PB19");
}

void mouse_scan(uint32_t now_ms) {
    static uint32_t last_ms;

    if (now_ms == last_ms) {
        return;
    }
    last_ms = now_ms;

    uint32_t pins = GPIOB_ReadPort();
    uint8_t button_changed = 0;

    s_scan_cnt++;

    uint8_t raw_btn = 0;
    if (!(pins & BTN_LEFT_PIN))   raw_btn |= HID_BTN_LEFT;
    if (!(pins & BTN_RIGHT_PIN))  raw_btn |= HID_BTN_RIGHT;
    if (!(pins & BTN_MID_PIN))    raw_btn |= HID_BTN_MID;
    if (!(pins & BTN_SIDE1_PIN))  raw_btn |= HID_BTN_BACK;
    if (!(pins & BTN_SIDE2_PIN))  raw_btn |= HID_BTN_FWD;

    if (raw_btn == s_btn_history) {
        if (s_btn_match < DEBOUNCE_SAMPLES) {
            s_btn_match++;
            if (s_btn_match == DEBOUNCE_SAMPLES && s_btn_stable != raw_btn) {
                s_btn_stable = raw_btn;
                button_changed = 1;
            }
        }
    } else {
        s_btn_history = raw_btn;
        s_btn_match = 0;
    }

    uint8_t buf[12];
    paw3395_burst(buf);

    int16_t dx = (int16_t)buf[2] | buf[3] << 8;
    int16_t dy = (int16_t)buf[4] | buf[5] << 8;
    int8_t  wheel = encoder_update(pins);

    /* Send when the buttons changed, the wheel turned, or we have motion. */
    if (button_changed || dx != 0 || dy != 0 || wheel != 0) {
        MouseReport_t rpt = {
            .buttons = s_btn_stable,
            .dx      = -dx,
            .dy      = -dy,
            .wheel   = wheel
        };

        s_pub_cnt++;
        transport_router_publish(&rpt);
    }

    if (now_ms - s_stat_ms >= 5000u) {
        s_stat_ms = now_ms;
        LOG_I("MOUSE", "scan %u pub %u in 5s", (unsigned)s_scan_cnt, (unsigned)s_pub_cnt);
        s_scan_cnt = 0;
        s_pub_cnt = 0;
    }
}