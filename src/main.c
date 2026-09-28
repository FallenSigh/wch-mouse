#include "CH58x_common.h"
#include "log.h"
#include "bmi270_port.h"
#include "bmi2_defs.h"
#include "ch585_usbhs_device.h"
#include "UART.h"
#include "mouse.h"
#include "air_mouse.h"
#include "motor.h"
#include "rgb.h"
#include "rgb_fx.h"
#include "oled.h"
#include "paw3395.h"
#include "paw3395_port.h"
#include "bat.h"
#include "transport.h"
#include "radio_mode.h"

#if defined(WCH_BLE_ENABLE) || defined(WCH_RF_ENABLE)
/* Radio stack includes (CMake adds these paths for WCH_BLE_ENABLE/WCH_RF_ENABLE).
 * The BLE HID and 2.4G RF transports both run on this vendor stack.
 * CONFIG.h declares MEM_BUF, MacAddr and pulls in CH58xBLE_LIB.h.
 * HAL.h pulls CONFIG.h / RTC.h / SLEEP.h and declares CH58x_BLEInit / HAL_Init. */
#include "CONFIG.h"
#include "HAL.h"
#endif

static volatile uint32_t s_tick_ms;

#ifdef WCH_DCDC_ENABLE
/* Factory hardware-config word, read before PWR_DCDCCfg() so we can report
 * whether it refused to enable the DC-DC. */
static uint32_t s_dcdc_hw[2];
#endif

#if defined(WCH_BLE_ENABLE) || defined(WCH_RF_ENABLE)
/* Protocol-stack heap. The PERI library's BLE_LibInit rejects anything below
 * 4 KiB; default is 6 KiB (BLE_MEMHEAP_SIZE in CONFIG.h). */
__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];
#endif

/* Default sensor resolution at boot (gaming-baseline CPI). The driver
 * clamps to its supported range [50, 26000]. */
#define MOUSE_DEFAULT_CPI   1600u

__INTERRUPT void TMR3_IRQHandler() {
    if (TMR3_GetITFlag(TMR0_3_IT_CYC_END)) {
        TMR3_ClearITFlag(TMR0_3_IT_CYC_END);
        s_tick_ms++;
    }
}

/* Millisecond clock handed to the logger for line timestamps. */
static uint32_t tick_ms(void) {
    return s_tick_ms;
}

#define BIO_RST_PIN          GPIO_Pin_11
#define BIO_RST_ACTIVE_LOW   1

void bio_reset(void)
{
    GPIOA_ModeCfg(BIO_RST_PIN, GPIO_ModeOut_PP_5mA);
    GPIOA_ResetBits(BIO_RST_PIN);   /* assert reset  */
    mDelaymS(20);
    GPIOA_SetBits(BIO_RST_PIN);     /* release reset */
    mDelaymS(100);                  /* module boot time */
}

static struct paw3395_dev paw;
static struct bmi2_dev bmi;

int main() {
    uint8_t len;

#ifdef WCH_DCDC_ENABLE
    /* The chip powers up on its pass-through LDO; switching on the internal
     * DC-DC drops run current to ~60% of that. Requires the 10 uH inductor
     * between VSW and VDCID to be fitted (present on this board). Vendor
     * order is to enable it before touching the clock configuration.
     *
     * PWR_DCDCCfg() silently does nothing when bit 13 of the factory
     * hardware-config word (ROM_CFG_ADR_HW) is set, so capture that word and
     * later report it together with the resulting RB_PWR_DCDC_EN state -
     * otherwise there is no way to tell "DC-DC on" from "call ignored". */
    FLASH_EEPROM_CMD(CMD_GET_ROM_INFO, ROM_CFG_ADR_HW, s_dcdc_hw, 0);
    PWR_DCDCCfg(ENABLE);
#endif

    HSECFG_Capacitance(HSECap_12p);
    SetSysClock(SYSCLK_FREQ);

#if defined(WCH_BLE_ENABLE) || defined(WCH_RF_ENABLE)
    /* Vendor init order: radio stack, then HAL task registration (which also
     * brings up the 32K clock for TMOS). GAPRole_PeripheralInit() is BLE-only
     * and runs below, once radio_mode_init() has picked this boot's mode. */
    CH58x_BLEInit();
    HAL_Init();
#endif

    GPIOPinRemap(DISABLE, RB_RF_ANT_SW_EN);

    /* Release the BIO module from reset before expecting its UART traffic. */
    bio_reset();

    // init uart1
    GPIOA_SetBits(GPIO_Pin_9);
    GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(GPIO_Pin_9, GPIO_ModeOut_PP_5mA);
    UART1_DefInit();

    log_init(LOG_LEVEL);
    log_set_clock(tick_ms);

    /* Which radio transport this boot runs, loaded from DataFlash. Must be
     * known before the transports are initialised. */
    radio_mode_init();

#ifdef WCH_DCDC_ENABLE
    LOG_I("PWR", "dcdc hw0=0x%08X bit13=%u plan=0x%04X enabled=%u",
          (unsigned)s_dcdc_hw[0], (unsigned)((s_dcdc_hw[0] >> 13) & 1u),
          (unsigned)R16_POWER_PLAN,
          (unsigned)((R16_POWER_PLAN & RB_PWR_DCDC_EN) ? 1u : 0u));
#endif

    /* Tim2 init */
    TMR2_TimerInit(FREQ_SYS / 10000);
    TMR2_ITCfg(ENABLE, TMR0_3_IT_CYC_END);
    PFIC_EnableIRQ(TMR2_IRQn);

    /* Usart3 init */
    UART3_Init( 1, DEF_UARTx_BAUDRATE, DEF_UARTx_STOPBIT, DEF_UARTx_PARITY  );

    /* UART3 feeds the BIO module's USB CDC bridge. With no host attached its
     * receive interrupt would fire once per incoming byte and accumulate data
     * nobody can read, so keep it off until USB comes up. */
    UART3_INTCfg(DISABLE, RB_IER_RECV_RDY | RB_IER_LINE_STAT);

    /* The USBHS PHY is now owned by transport_usb: it powers up only while
     * VBUS is present, so it does not burn 10-20 mA on battery. */

    mouse_init(&paw);

    /* The OLED bring-up blocks for a few hundred ms (reset settle plus the
     * first full-screen flush), so it runs before the USB device layer is up.
     * Blocking later would starve the SETUP handling the host needs during
     * enumeration and USB would never take over. */
    if (oled_init()) {
        oled_demo();
    }

#ifdef WCH_BLE_ENABLE
    /* Registering the GAP role must happen before the main loop pumps TMOS:
     * the BLE transport's init (called by transport_router_init below) queues
     * the event that starts the peripheral role and advertising. Skipped when
     * this boot runs the 2.4G RF mode instead. */
    if (radio_mode_is(RADIO_MODE_BLE)) {
        bStatus_t s = GAPRole_PeripheralInit();
        (void)s;
    }
#endif

    transport_router_init();

    /* TIM3 at 1 kHz for the mouse tick. TIM0 is reserved for the RF transport,
     * and TIM3 is free here because the BLE HAL only claims TMR3_IRQHandler
     * when RF_8K is defined (we don't use the 8k RF mode). */
    TMR3_TimerInit(FREQ_SYS / 1000);
    TMR3_ITCfg(ENABLE, TMR0_3_IT_CYC_END);
    PFIC_EnableIRQ(TMR3_IRQn);

    rgb_init();
    motor_init();
    paw3395_port_init(&paw);
    paw3395_set_cpi(&paw, 800);
    paw3395_set_mode(&paw, PAW3395_MODE_HIGH_PERFORMANCE);
    paw3395_set_lift_cut(&paw, PAW3395_LIFT_CUT_2MM);
    bat_init();

    /* The IMU is only needed for air-mouse mode, so it stays with both sensors
     * disabled here: air_mouse enables the gyroscope on entry and the exit path
     * disables it again, leaving the IMU idle in normal use. bmi270_init() only
     * loads the config, it does not enable a sensor. */
    if (bmi270_port_init(&bmi) != BMI2_OK) {
        LOG_E("BMI270", "init failed");
    } else {
        LOG_I("BMI270", "init done (sensors off)");
    }

    /* Air-mouse mode: side-1 + side-2 toggle the cursor between the optical
     * sensor and the gyro. It re-applies this boot's optical geometry after
     * its own paw3395_init(), so hand it the same values used above. */
    const air_mouse_cfg_t air_cfg = {
        .paw  = &paw,
        .bmi  = &bmi,
        .cpi  = 800,
        .mode = PAW3395_MODE_HIGH_PERFORMANCE,
        .lift = PAW3395_LIFT_CUT_2MM,
    };
    air_mouse_init(&air_cfg);

    LOG_I("MAIN", "init done");

    bool bio_host = false;
    while(1) {
#if defined(WCH_BLE_ENABLE) || defined(WCH_RF_ENABLE)
        /* TMOS tick is 625 us and MUST be pumped from main-loop context;
         * calling it from an ISR corrupts the scheduler. */
        TMOS_SystemProcess();
#endif

        /* UART3 <-> USB CDC bridge: it only has a consumer while a USB host
         * is really attached, and it pokes USB endpoint registers, so skip
         * both halves unless USB is the active link. The BIO module's receive
         * interrupt is gated with it. */
        {
            const bool usb_host = (transport_router_active() == TR_USB);

            if (usb_host != bio_host) {
                bio_host = usb_host;
                if (usb_host) {
                    UART3_INTCfg(ENABLE, RB_IER_RECV_RDY | RB_IER_LINE_STAT);
                } else {
                    UART3_INTCfg(DISABLE, RB_IER_RECV_RDY | RB_IER_LINE_STAT);
                    CDC.Uart_RecLen    = 0;   /* drop data buffered with no host */
                    CDC.Uart_Input_Ptr = 0;
                    CDC.Uart_Output_Ptr = 0;
                }
            }

            if (usb_host) {
                UART3_DataRx_Deal();
                UART3_DataTx_Deal();
            }
        }

        mouse_scan(s_tick_ms);

        radio_mode_poll(s_tick_ms);
        transport_router_poll(s_tick_ms);

        bat_poll(s_tick_ms);

        /* RGB underglow: idle hue cycle, radio-mode announce and the pending
         * switch preview are all inside rgb_fx. */
        rgb_fx_poll(s_tick_ms, radio_mode_is(RADIO_MODE_BLE), radio_mode_armed(),
                    air_mouse_active());
        rgb_poll(s_tick_ms);
    }

    return 0;
}
