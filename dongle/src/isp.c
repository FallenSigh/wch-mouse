/*
 * dongle/src/isp.c - drop the dongle into the ROM ISP bootloader on request.
 *
 * Same trick as the mouse (src/app/isp.c): the ROM has no call that enters ISP
 * directly, so it decides between running the user code and waiting for a
 * download by checking whether the first bytes of CodeFlash hold a program. The
 * way in from the application is therefore to erase the entry block and reset -
 * the ROM then finds no valid image and parks in ISP.
 *
 * The difference is the trigger. The dongle has no BOOT strap and, once it is
 * in its case, no way to hold one anyway, so the boot-time power-cycle flow is
 * impractical. Instead the request comes over USB - the vendor Feature report is
 * the one channel the case still exposes - from rf_dongle.c's proto_handle_set.
 * That runs in the USB ISR, so it only sets a flag and isp_poll() does the
 * erase/reset from the main loop.
 *
 * The erase is destructive by design (the image is about to be replaced), but
 * it only touches CodeFlash block 0. The dongle has no DataFlash record of its
 * own, and the mouse's radio mode/settings are on the mouse, not here.
 */

#include "isp.h"

#include <stdbool.h>

#include "CH58x_common.h"
#include "ISP585.h"
#include "log.h"

static volatile bool s_isp_req;

void isp_request(void) {
    s_isp_req = true;
}

/* Erasing the entry block pulls the ground out from under this code, so the
 * whole path has to run from RAM and interrupts stay off until the reset. The
 * reset sequence is the published one for this trick. */
__HIGH_CODE
static void isp_enter(void) {
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

void isp_init(void) {
    s_isp_req = false;
}

void isp_poll(void) {
    if (s_isp_req) {
        isp_enter();
    }
}
