#include "CH58x_common.h"
#include "log.h"
#include "ch585_usbhs_device.h"
#include "UART.h"
#include "rf_dongle.h"

#include "CONFIG.h"
#include "HAL.h"

/* Protocol-stack heap (see wch-mouse/src/main.c). */
__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];

int main(void) {
    HSECFG_Capacitance(HSECap_10p);
    SetSysClock(SYSCLK_FREQ);

    /* Debug UART1 on PA8/PA9, matching wch-mouse. */
    GPIOA_SetBits(GPIO_Pin_9);
    GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(GPIO_Pin_9, GPIO_ModeOut_PP_5mA);
    UART1_DefInit();

    log_init(LOG_LEVEL);

    /* Radio stack (shared by the BLE and RF transports). */
    CH58x_BLEInit();
    HAL_Init();

    /* USB HID mouse device. */
    USBHS_Device_Init(ENABLE);

    rf_dongle_init();

    LOG_I("DONGLE", "ready");

    while (1) {
        TMOS_SystemProcess(); /* 625 us tick, main-loop context only */
        rf_dongle_poll();
    }
}
