/*
 * src/app/isp.c - drop into the ROM ISP bootloader on request.
 *
 * The ROM has no call that enters ISP directly; it decides between running the
 * user code and waiting for a download by checking whether the first bytes of
 * CodeFlash hold a program. So the way in from the application is to erase the
 * entry block and reset - the ROM then finds no valid image and parks in ISP.
 *
 * The BOOT strap (PB22) is an ordinary GPIO once running, so holding it is the
 * trigger, gated on VBUS: a stray press on battery cannot wipe the firmware.
 * The erase is destructive by design (the image is about to be replaced), but
 * it only touches CodeFlash block 0, so the DataFlash record - radio mode,
 * settings, bonds - survives.
 */

#include "isp.h"

#include <stdbool.h>

#include "CH58x_common.h"
#include "ISP585.h"
#include "bat.h"
#include "log.h"

#define ISP_BTN_PIN      GPIO_Pin_22
#define ISP_BTN_HOLD_MS  3000u

static uint32_t s_btn_ms;
static bool     s_btn_held;

/* Erasing the entry block pulls the ground out from under this code, so the
 * whole path has to run from RAM and interrupts stay off until the reset. The
 * reset sequence is the published one for this trick. */
__HIGH_CODE
static void isp_enter(void)
{
    uint32_t irq_status;

    LOG_I("ISP", "entering ROM ISP");
    SYS_DisableAllIrq(&irq_status);

    while (FLASH_ROM_ERASE(0, EEPROM_BLOCK_SIZE) != 0u) {
        ;
    }

    FLASH_ROM_SW_RESET();
    R8_SAFE_ACCESS_SIG = SAFE_ACCESS_SIG1;
    R8_SAFE_ACCESS_SIG = SAFE_ACCESS_SIG2;
    SAFEOPERATE;
    R16_INT32K_TUNE = 0xFFFF;
    R8_RST_WDOG_CTRL |= RB_SOFTWARE_RESET;
    R8_SAFE_ACCESS_SIG = 0;

    while (1) {
        ;
    }
}

void isp_init(void)
{
    GPIOB_ModeCfg(ISP_BTN_PIN, GPIO_ModeIN_PU);
}

void isp_poll(uint32_t now_ms)
{
    bool pressed;

    /* Only a cabled host can reflash and this is not self-recoverable, so the
     * strap is ignored entirely while running on battery. */
    if (!bat_power_good()) {
        s_btn_held = false;
        return;
    }

    pressed = (GPIOB_ReadPortPin(ISP_BTN_PIN) == 0u);

    if (pressed != s_btn_held) {
        s_btn_held = pressed;
        s_btn_ms   = now_ms;
        return;
    }

    if (s_btn_held && (now_ms - s_btn_ms) >= ISP_BTN_HOLD_MS) {
        isp_enter();
    }
}
