#include "CH58x_common.h"
#include "log.h"
#include "bmi2.h"
#include "bmi270.h"
#include "bmi270_port.h"
#include "bmi2_defs.h"
#include "ch585_usbhs_device.h"
#include "UART.h"
#include "mouse.h"
#include "rgb.h"
#include "paw3395.h"
#include "bat.h"
#include "transport.h"

#ifdef WCH_BLE_ENABLE
/* Vendor BLE stack includes (paths added by CMake when WCH_BLE_ENABLE=ON).
 * CONFIG.h declares MEM_BUF, MacAddr and pulls in CH58xBLE_LIB.h.
 * HAL.h pulls CONFIG.h / RTC.h / SLEEP.h and declares CH58x_BLEInit / HAL_Init. */
#include "CONFIG.h"
#include "HAL.h"
/* GAPRole_PeripheralInit / TMOS_SystemProcess come from CH58xBLE_LIB.h. */
#endif

static volatile uint32_t s_tick_ms;

#ifdef WCH_DCDC_ENABLE
/* Factory hardware-config word, read before PWR_DCDCCfg() so we can report
 * whether it refused to enable the DC-DC. */
static uint32_t s_dcdc_hw[2];
#endif

#ifdef WCH_BLE_ENABLE
/* BLE protocol-stack heap. The PERI library's BLE_LibInit rejects anything
 * below 4 KiB; default is 6 KiB (BLE_MEMHEAP_SIZE in CONFIG.h). The vendor
 * HID_Mouse example uses the same alignment attribute. */
__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];
#endif

/* Default sensor resolution at boot (gaming-baseline CPI). The driver
 * clamps to its supported range [50, 26000]. */
#define MOUSE_DEFAULT_CPI   1600u

__INTERRUPT void TMR0_IRQHandler() {
    if (TMR0_GetITFlag(TMR0_3_IT_CYC_END)) {
        TMR0_ClearITFlag(TMR0_3_IT_CYC_END);
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

#ifdef WCH_BLE_ENABLE
    /* Vendor init order: radio stack, then HAL task registration (which also
     * brings up the 32K clock for TMOS), then the peripheral GAP role.
     * GAPRole_PeripheralInit only registers the role - it does NOT start
     * advertising. The HID/Battery/DeviceInfo GATT profile and the
     * transport_ble.init() chain run later via transport_router_init(),
     * which calls transport_ble.init() -> HidDev_Init() -> Hid_AddService()
     * -> Batt_Setup() etc., and then tmos_set_event schedules the first
     * GAPRole_PeripheralStartDevice (advertising). */
    CH58x_BLEInit();
    HAL_Init();
    {
        bStatus_t s = GAPRole_PeripheralInit();
        (void)s;
    }
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

    mouse_init();
    transport_router_init();

    /* TIM0 at 1 kHz for the mouse tick. Deliberately NOT TIM3: the BLE HAL
     * hijacks TMR3_IRQHandler when RF_8K is defined, which would silently
     * freeze this tick in tri-mode builds. */
    TMR0_TimerInit(FREQ_SYS / 1000);
    TMR0_ITCfg(ENABLE, TMR0_3_IT_CYC_END);
    PFIC_EnableIRQ(TMR0_IRQn);

    rgb_init();
    paw3395_init();
    paw3395_set_cpi(800);
    bat_init();

    if (bmi270_port_init(&bmi) != BMI2_OK) {
        LOG_E("BMI270", "init failed");
    } else {
        LOG_I("BMI270", "init done");
    }

    struct bmi2_sens_config cfg = { 0 };
    cfg.type                = BMI2_ACCEL;
    cfg.cfg.acc.odr         = BMI2_ACC_ODR_100HZ;    /* 100 Hz          */
    cfg.cfg.acc.bwp         = BMI2_ACC_NORMAL_AVG4;
    cfg.cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;
    cfg.cfg.acc.range       = BMI2_ACC_RANGE_8G;      /* ±8g → 4096 LSB/g */
    if (bmi2_set_sensor_config(&cfg, 1, &bmi) != BMI2_OK) {
        LOG_W("BMI270", "set config failed");
    } 

    uint8_t sens_list[2] = { BMI2_ACCEL, BMI2_GYRO };
    if (bmi270_sensor_enable(sens_list, 2, &bmi) != BMI2_OK) {
        LOG_W("BMI270", "sensor enable failed");
    }

    LOG_I("MAIN", "init done");

    uint32_t last_btn_scan = 0;
    uint8_t data[12];
    bool bio_host = false;
    while(1) {
#ifdef WCH_BLE_ENABLE
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

        if (s_tick_ms - last_btn_scan >= 1) {
            last_btn_scan = s_tick_ms;
            mouse_scan(s_tick_ms);
        }

        transport_router_poll(s_tick_ms);

        bat_poll(s_tick_ms);

        /* IMU snapshot every 5 s: accel in raw counts and mg (±8g: 4096 LSB/g),
         * gyro in raw counts (±2000 dps default: 16.4 LSB/dps). */
        static uint32_t last_imu_ms = 0;
        if (s_tick_ms - last_imu_ms >= 5000) {
            last_imu_ms = s_tick_ms;
            struct bmi2_sens_data d;
            if (bmi2_get_sensor_data(&d, &bmi) == BMI2_OK) {
                LOG_I("BMI270", "acc=(%d,%d,%d) mg=(%d,%d,%d) gyr=(%d,%d,%d)",
                      d.acc.x, d.acc.y, d.acc.z,
                      d.acc.x * 1000 / 4096, d.acc.y * 1000 / 4096, d.acc.z * 1000 / 4096,
                      d.gyr.x, d.gyr.y, d.gyr.z);
            }
        }

        /* RGB underglow: cycle the hue at ~30 Hz. The TIM1+DMA driver does
         * all the bit timing in hardware, so this loop just edits the
         * colour buffer and triggers a one-frame DMA refresh. No global
         * IRQ disable, no impact on USB. */
        static uint32_t last_rgb_ms = 0;
        static uint16_t rgb_hue    = 0;
        if (s_tick_ms - last_rgb_ms >= 33) {
            last_rgb_ms = s_tick_ms;
            rgb_set_brightness(1);
            uint8_t r, g, b;
            uint8_t region = (uint8_t)(rgb_hue / 60);
            uint8_t rem    = (uint8_t)((rgb_hue % 60) * 255U / 60U);
            switch (region) {
                case 0: r = 255;     g = rem;       b = 0;   break;
                case 1: r = 255 - rem; g = 255;       b = 0;   break;
                case 2: r = 0;       g = 255;       b = rem; break;
                case 3: r = 0;       g = 255 - rem; b = 255; break;
                case 4: r = rem;     g = 0;         b = 255; break;
                default: r = 255;    g = 0;         b = 255 - rem; break;
            }
            rgb_set_all(r, g, b);
            rgb_show();
            rgb_hue = (rgb_hue + 1) % 360;
        }
    }

    return 0;
}
