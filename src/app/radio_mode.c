/*
 * src/app/radio_mode.c - which radio transport this boot runs.
 *
 * RF (the dongle link) is preferred; BLE is the fallback for hosts without a
 * receiver. Both run on the same radio and the vendor stack exposes no proven
 * live switch (RFRole_SwitchMode has no example and requires an idle radio),
 * so the mode is applied at boot: side 1 and the wheel click double as the
 * selector, and a long press stores the other mode and resets into it.
 *
 * Only a dual-transport image has a choice to make. A single-radio build fixes
 * its mode at compile time, so a stale DataFlash value can never disable the
 * only backend that build carries.
 */

#include "radio_mode.h"

#include "CH58x_common.h"
#include "log.h"
#include "motor.h"
#include "nvm.h"

#if defined(WCH_BLE_ENABLE) && defined(WCH_RF_ENABLE)
#define RADIO_MODE_SELECTABLE 1
#else
#define RADIO_MODE_SELECTABLE 0
#endif

#if RADIO_MODE_SELECTABLE

/* The mode flag sits in the first 4 KiB DataFlash block; nvm owns the offset
 * addressing, the magic and the block erase. The vendor keeps its BLE SNV in
 * the last block, so nothing collides. */
static const nvm_slot_t s_slot = {0x0000u, 0x5Au, 1u};

/* The switch is deferred to the release, so the reset never races the buttons
 * still being down. */
#define MODE_BTN_HOLD_MS 3000u

static uint32_t s_btn_ms;
static bool s_btn_held;
static bool s_btn_armed;
static uint32_t s_reset_at; /* nonzero once the switch has been stored */

static radio_mode_t radio_mode_load(void) {
    uint8_t mode;

    if (!nvm_load(&s_slot, &mode)) {
        return RADIO_MODE_RF;
    }

    return (mode == RADIO_MODE_BLE) ? RADIO_MODE_BLE : RADIO_MODE_RF;
}

static void radio_mode_store(radio_mode_t mode) {
    const uint8_t flag = (uint8_t)mode;

    (void)nvm_save(&s_slot, &flag);

    LOG_I("MODE", "store %s", (mode == RADIO_MODE_BLE) ? "BLE" : "RF");
}

#endif /* RADIO_MODE_SELECTABLE */

static radio_mode_t s_mode;

void radio_mode_init(void) {
#if RADIO_MODE_SELECTABLE
    s_mode = radio_mode_load();
    LOG_I("MODE", "radio %s", (s_mode == RADIO_MODE_BLE) ? "BLE" : "RF");
#elif defined(WCH_RF_ENABLE)
    s_mode = RADIO_MODE_RF;
    LOG_I("MODE", "radio RF");
#elif defined(WCH_BLE_ENABLE)
    s_mode = RADIO_MODE_BLE;
    LOG_I("MODE", "radio BLE");
#else
    s_mode = RADIO_MODE_BLE; /* no radio in this build */
#endif
}

radio_mode_t radio_mode_get(void) {
    return s_mode;
}

bool radio_mode_is(radio_mode_t mode) {
    return s_mode == mode;
}

bool radio_mode_armed(void) {
#if RADIO_MODE_SELECTABLE
    return s_btn_armed;
#else
    return false;
#endif
}

void radio_mode_poll(uint32_t now_ms, bool combo_held) {
#if RADIO_MODE_SELECTABLE
    bool pressed = combo_held;

    /* The reset waits for the switch buzz to finish: it pulls PB0 down, so the
     * motor could not have outlived it. */
    if ((s_reset_at != 0u) && ((int32_t)(now_ms - s_reset_at) >= 0)) {
        SYS_ResetExecute();
    }

    if (pressed != s_btn_held) {
        s_btn_held = pressed;
        s_btn_ms = now_ms;

        if (pressed) {
            s_btn_armed = false;
        } else if (s_btn_armed) {
            radio_mode_t next = (s_mode == RADIO_MODE_BLE) ? RADIO_MODE_RF : RADIO_MODE_BLE;

            LOG_I("MODE", "switch to %s", (next == RADIO_MODE_BLE) ? "BLE" : "RF");
            radio_mode_store(next);

            /* Announce it, then reset when the buzz ends. Non-blocking, and long
             * enough for the log line above to drain on its own. */
            motor_pulse(MOTOR_MODE_SWITCH_MS);
            s_reset_at = now_ms + MOTOR_MODE_SWITCH_MS;
        }
        return;
    }

    if (s_btn_held && !s_btn_armed && (now_ms - s_btn_ms >= MODE_BTN_HOLD_MS)) {
        s_btn_armed = true;
        LOG_I("MODE", "release to switch radio mode");
    }
#else
    (void)now_ms;
    (void)combo_held;
#endif
}
