#include "CH58x_common.h"
#include "board.h"
#include "log.h"
#include "bmi270_port.h"
#include "bmi2_defs.h"
#include "ch585_usbhs_device.h"
#include "UART.h"
#include "mouse.h"
#include "air_mouse.h"
#include "motor.h"
#include "power.h"
#include "bio.h"
#include "proto.h"
#include "settings.h"
#include "rgb.h"
#include "rgb_fx.h"
#include "oled.h"
#include "paw3395.h"
#include "paw3395_port.h"
#include "bat.h"
#include "isp.h"
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

/* The base tick follows the active link. A USB host polls the HID endpoint at
 * 8 kHz and can consume everything; BLE is limited by its connection interval
 * (7.5-10 ms), where an 8 kHz tick would only stop the core from ever idling.
 * 1 kHz is the floor: the millisecond clock is derived from the tick. */
#define TICK_HZ_FAST 8000u
#define TICK_HZ_SLOW 1000u

static volatile uint32_t s_tick;    /* s_tick_hz units */
static volatile uint32_t s_tick_ms; /* milliseconds    */
static volatile uint32_t s_ticks_per_ms;
static uint32_t s_tick_hz;

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

__INTERRUPT void TMR3_IRQHandler() {
    if (TMR3_GetITFlag(TMR0_3_IT_CYC_END)) {
        TMR3_ClearITFlag(TMR0_3_IT_CYC_END);
        if ((++s_tick % s_ticks_per_ms) == 0u) {
            s_tick_ms++;
        }
    }
}

/* Millisecond clock handed to the logger for line timestamps. */
static uint32_t tick_ms(void) {
    return s_tick_ms;
}

/* Restart TIM3 at the current base rate. Needed after a standby cycle, where
 * the core clock and the timer both stopped. */
static void tick_rearm(void) {
    TMR3_TimerInit(FREQ_SYS / s_tick_hz);
    TMR3_ITCfg(ENABLE, TMR0_3_IT_CYC_END);
    PFIC_EnableIRQ(TMR3_IRQn);
}

/* s_tick is never reset, so the report cadence keeps its phase across a link
 * change instead of firing immediately on the new rate. */
static void tick_set_rate(uint32_t hz) {
    if (hz == s_tick_hz) {
        return;
    }

    s_tick_hz = hz;
    s_ticks_per_ms = hz / 1000u;

    tick_rearm();
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

    /* Bring the BIO module up before expecting its UART traffic. */
    bio_init();
    bio_reset();

    // init uart1
    GPIOA_SetBits(BOARD_UART1_TX_PIN);
    GPIOA_ModeCfg(BOARD_UART1_RX_PIN, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(BOARD_UART1_TX_PIN, GPIO_ModeOut_PP_5mA);
    UART1_DefInit();

    log_init(LOG_LEVEL);
    log_set_clock(tick_ms);

    /* Which radio transport this boot runs, loaded from DataFlash. Must be
     * known before the transports are initialised. */
    radio_mode_init();
    isp_init();

#ifdef WCH_DCDC_ENABLE
    LOG_I("PWR", "dcdc hw0=0x%08X bit13=%u plan=0x%04X enabled=%u", (unsigned)s_dcdc_hw[0],
          (unsigned)((s_dcdc_hw[0] >> 13) & 1u), (unsigned)R16_POWER_PLAN,
          (unsigned)((R16_POWER_PLAN & RB_PWR_DCDC_EN) ? 1u : 0u));
#endif

    /* Tim2 init */
    TMR2_TimerInit(FREQ_SYS / 10000);
    TMR2_ITCfg(ENABLE, TMR0_3_IT_CYC_END);
    PFIC_EnableIRQ(TMR2_IRQn);

    /* Usart3 init */
    UART3_Init(1, DEF_UARTx_BAUDRATE, DEF_UARTx_STOPBIT, DEF_UARTx_PARITY);

    /* UART3 receives the BIO module's real-time packets. Receive stays enabled
     * even without a host because bio_rt_feed() parses them locally for the
     * OLED; only the USB CDC forwarding below is gated on USB. */

    /* The USBHS PHY is now owned by transport_usb: it powers up only while
     * VBUS is present, so it does not burn 10-20 mA on battery. */

    mouse_init();

    /* Load the stored configuration first: it decides whether the panel comes
     * up at all, and the OLED bring-up blocks for a few hundred ms (reset
     * settle plus the first full-screen flush), so it has to run before the USB
     * device layer - blocking later would starve the SETUP handling the host
     * needs during enumeration and USB would never take over. */
    settings_init(&paw);

    /* The panel, the LED rail and the BIO module only run while tethered, so
     * this applies that policy here - before the USB device layer, because the
     * panel bring-up blocks long enough to starve the SETUP handling that
     * enumeration needs. */
    settings_apply_peripherals();

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

    /* TIM3 drives both the ms clock and the report cadence. TIM0 is reserved for
     * the RF transport, and TIM3 is free here because the BLE HAL only claims
     * TMR3_IRQHandler when RF_8K is defined (we don't use it). The rate itself
     * follows the active link and is picked in the main loop. */
    tick_set_rate(TICK_HZ_FAST);

    rgb_init();
    /* rgb_init() leaves the LED rail powered; the rail is dropped from the
     * product, so cut it once here and let rgb_fx_poll() keep it off. */
    rgb_set_enable(false);
    motor_init();
    paw3395_port_init(&paw);

    /* Apply the stored configuration before the PA7 motion interrupt is armed,
     * then start reading the sensor. */
    settings_apply();
    paw3395_motion_start();

    power_init();

    proto_init();
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
     * sensor and the gyro. It re-applies the optical geometry after its own
     * paw3395_init(), so hand it the same persisted configuration that
     * settings_apply() just programmed rather than the built-in defaults -
     * otherwise leaving the mode would clobber the user's CPI/mode/lift. */
    const air_mouse_cfg_t air_cfg = {
        .paw = &paw,
        .bmi = &bmi,
        .cpi = settings_cpi(),
        .mode = (enum paw3395_mode)settings_mode(),
        .lift = (enum paw3395_lift_cut)settings_lift(),
        .sens_idx = settings_air_sens_idx(),
        .odr_idx = settings_air_odr_idx(),
    };
    air_mouse_init(&air_cfg);

    LOG_I("MAIN", "init done");

    while (1) {
#if defined(WCH_BLE_ENABLE) || defined(WCH_RF_ENABLE)
        /* TMOS tick is 625 us and MUST be pumped from main-loop context;
         * calling it from an ISR corrupts the scheduler. */
        TMOS_SystemProcess();
#endif

        /* UART3 <-> USB CDC bridge. The BIO real-time packets are parsed
         * locally in the UART3 ISR, so the ring only matters to a host: drain
         * it when USB owns the link, otherwise drop it so it cannot overflow. */
        if (transport_router_active() == TR_USB) {
            UART3_DataRx_Deal();
            UART3_DataTx_Deal();
        } else {
            CDC.Uart_RecLen = 0;
            CDC.Uart_Input_Ptr = 0;
            CDC.Uart_Output_Ptr = 0;
        }

        /* Both the base tick and the report cadence follow the active link: the
         * USB-oriented setting must not drive the scan loop over BLE, whose
         * ceiling is the connection interval. */
        const transport_id_t link = transport_router_active();

        tick_set_rate((link == TR_BLE) ? TICK_HZ_SLOW : TICK_HZ_FAST);

        {
            static uint32_t last_report_tick;
            uint32_t rate;
            uint32_t div;

            /* Each link keeps its own persisted rate: the unsuffixed pair is the
             * USB one, RF and BLE have their own. */
            if (link == TR_BLE) {
                rate = settings_report_hz_ble();
            } else if (link == TR_RF24) {
                rate = settings_report_hz_rf();
            } else {
                rate = settings_report_hz();
            }

            div = s_tick_hz / rate;

            if (div == 0u) {
                div = 1u;
            }

            if ((uint32_t)(s_tick - last_report_tick) >= div) {
                /* Advance by whole periods only: resetting to s_tick would throw
                 * away the remainder and degrade the cadence to one scan per
                 * loop iteration (6300/s instead of the requested 4000/s). */
                last_report_tick += ((s_tick - last_report_tick) / div) * div;
                mouse_scan(s_tick_ms);
            }
        }

        /* Standby: parks the peripherals and sleeps the MCU once the input has
         * been quiet long enough. Waking restores the system clock and the
         * flash, so everything derived from them is re-armed right here. */
        if (power_poll(s_tick_ms)) {
            tick_rearm();

            TMR2_TimerInit(FREQ_SYS / 10000);
            TMR2_ITCfg(ENABLE, TMR0_3_IT_CYC_END);
            PFIC_EnableIRQ(TMR2_IRQn);

            UART3_Init(1, DEF_UARTx_BAUDRATE, DEF_UARTx_STOPBIT, DEF_UARTx_PARITY);
        }

        isp_poll(s_tick_ms);
        transport_router_poll(s_tick_ms);

        bat_poll(s_tick_ms);
        motor_poll(s_tick_ms);
        settings_poll(s_tick_ms);

        /* BIO real-time screen: refresh only on a fresh packet (~1.28 s) and
         * only when it carries a real measurement, so the panel keeps showing
         * the last valid reading instead of a "no contact" packet. */
        {
            static uint32_t last_rt_seq;
            const uint32_t rt_seq = bio_rt_seq();

            if (rt_seq != last_rt_seq) {
                bio_rt_pack_t pkt;

                last_rt_seq = rt_seq;
                if (oled_enabled() && bio_rt_read(&pkt) && bio_rt_pack_valid(&pkt)) {
                    oled_bio_show(pkt.heartrate, pkt.spo2, pkt.bk, pkt.sdnn);
                }
            }
        }

        /* Keep the battery gauge up when no BIO packet is being drawn: it is
         * the only thing on an otherwise blank panel. One full flush a second,
         * which is the cost of having it there without BIO data. */
        {
            static uint32_t last_volt_ms;

            if (oled_enabled() && ((uint32_t)(s_tick_ms - last_volt_ms) >= 1000u)) {
                last_volt_ms = s_tick_ms;
                (void)oled_show_voltage();
            }
        }

        /* RGB underglow: idle hue cycle, radio-mode announce and the pending
         * switch preview are all inside rgb_fx. */
        rgb_fx_poll(s_tick_ms, radio_mode_is(RADIO_MODE_BLE), radio_mode_armed(),
                    air_mouse_active());
        rgb_poll(s_tick_ms);
    }

    return 0;
}
