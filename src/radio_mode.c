/*
 * src/radio_mode.c - which radio transport this boot runs.
 *
 * RF (the dongle link) is preferred; BLE is the fallback for hosts without a
 * receiver. Both run on the same radio and the vendor stack exposes no proven
 * live switch (RFRole_SwitchMode has no example and requires an idle radio),
 * so the mode is applied at boot: the BOOT strap pin doubles as the selector,
 * and a long press stores the other mode and resets into it.
 *
 * Only a dual-transport image has a choice to make. A single-radio build fixes
 * its mode at compile time, so a stale DataFlash value can never disable the
 * only backend that build carries.
 */

#include "radio_mode.h"

#include "CH58x_common.h"
#include "ISP585.h"
#include "log.h"

#if defined(WCH_BLE_ENABLE) && defined(WCH_RF_ENABLE)
#define RADIO_MODE_SELECTABLE 1
#else
#define RADIO_MODE_SELECTABLE 0
#endif

#if RADIO_MODE_SELECTABLE

/* The EEPROM_* ROM commands address the DataFlash by OFFSET, not by the
 * absolute 0x00070000 the memory map shows - EVT/EXAM/FLASH writes at offset
 * 0, and the vendor's own BLE_SNV_ADDR is 0x77000 - FLASH_ROM_MAX_SIZE =
 * 0x7000. Passing the absolute address gets the whole command rejected. Our
 * record sits in the first 4 KiB block; the vendor keeps its BLE SNV in the
 * last one, so nothing collides.
 *
 * One erased block holds [magic, mode]; the magic tells a programmed block
 * apart from the 0xFF an erase leaves behind. */
#define RADIO_MODE_FLASH_OFF    0x0000u
#define RADIO_MODE_MAGIC        0x5Au

/* PB22 is the BOOT strap, sampled only at reset, so pressing it while running
 * costs nothing. The switch is deferred to the release: resetting while the
 * pin is still low would restart into the ISP bootloader, not the app. */
#define MODE_BTN_PIN            GPIO_Pin_22
#define MODE_BTN_HOLD_MS        3000u

static uint32_t s_btn_ms;
static bool     s_btn_held;
static bool     s_btn_armed;

/* The ROM's DataFlash commands require the buffer to be in RAM, 4-byte
 * aligned, and a 4-byte multiple in length - the vendor HAL's own flash
 * callback (Lib_Write_Flash) follows exactly that shape. Small misaligned
 * stack arrays make ERASE/WRITE return non-zero and silently change nothing. */
static radio_mode_t radio_mode_load(void)
{
    __attribute__((aligned(4))) uint8_t raw[4] = { 0xFFu, 0xFFu, 0xFFu, 0xFFu };

    EEPROM_READ(RADIO_MODE_FLASH_OFF, raw, sizeof(raw));

    if (raw[0] != RADIO_MODE_MAGIC) {
        return RADIO_MODE_RF;
    }

    return (raw[1] == RADIO_MODE_BLE) ? RADIO_MODE_BLE : RADIO_MODE_RF;
}

static void radio_mode_store(radio_mode_t mode)
{
    __attribute__((aligned(4))) uint8_t raw[4] = { RADIO_MODE_MAGIC, (uint8_t)mode, 0u, 0u };
    uint32_t er;
    uint32_t wr;

    /* Erase a whole 4 KiB block, exactly as the vendor HAL's working flash
     * callback (Lib_Write_Flash) does, rather than the 256-byte page the
     * header's EEPROM_PAGE_SIZE suggests. */
    er = EEPROM_ERASE(RADIO_MODE_FLASH_OFF, EEPROM_BLOCK_SIZE);
    wr = EEPROM_WRITE(RADIO_MODE_FLASH_OFF, raw, sizeof(raw));

    LOG_I("MODE", "store %s er=%u wr=%u", (mode == RADIO_MODE_BLE) ? "BLE" : "RF",
          (unsigned)er, (unsigned)wr);
}

#endif /* RADIO_MODE_SELECTABLE */

static radio_mode_t s_mode;

void radio_mode_init(void)
{
#if RADIO_MODE_SELECTABLE
    s_mode = radio_mode_load();
    GPIOB_ModeCfg(MODE_BTN_PIN, GPIO_ModeIN_PU);
    LOG_I("MODE", "radio %s", (s_mode == RADIO_MODE_BLE) ? "BLE" : "RF");
#elif defined(WCH_RF_ENABLE)
    s_mode = RADIO_MODE_RF;
    LOG_I("MODE", "radio RF");
#elif defined(WCH_BLE_ENABLE)
    s_mode = RADIO_MODE_BLE;
    LOG_I("MODE", "radio BLE");
#else
    s_mode = RADIO_MODE_BLE;   /* no radio in this build */
#endif
}

radio_mode_t radio_mode_get(void)
{
    return s_mode;
}

bool radio_mode_is(radio_mode_t mode)
{
    return s_mode == mode;
}

bool radio_mode_armed(void)
{
#if RADIO_MODE_SELECTABLE
    return s_btn_armed;
#else
    return false;
#endif
}

void radio_mode_poll(uint32_t now_ms)
{
#if RADIO_MODE_SELECTABLE
    bool pressed = (GPIOB_ReadPortPin(MODE_BTN_PIN) == 0u);

    if (pressed != s_btn_held) {
        s_btn_held = pressed;
        s_btn_ms   = now_ms;

        if (pressed) {
            s_btn_armed = false;
        } else if (s_btn_armed) {
            radio_mode_t next = (s_mode == RADIO_MODE_BLE) ? RADIO_MODE_RF : RADIO_MODE_BLE;

            LOG_I("MODE", "switch to %s", (next == RADIO_MODE_BLE) ? "BLE" : "RF");
            radio_mode_store(next);
            mDelaymS(100);   /* let the UART drain the log before resetting */
            SYS_ResetExecute();
        }
        return;
    }

    if (s_btn_held && !s_btn_armed && (now_ms - s_btn_ms >= MODE_BTN_HOLD_MS)) {
        s_btn_armed = true;
        LOG_I("MODE", "release to switch radio mode");
    }
#else
    (void)now_ms;
#endif
}
