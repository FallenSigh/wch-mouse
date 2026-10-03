#include "bat.h"
#include "CH58x_common.h"
#include "board.h"
#include "log.h"

#define BAT_ADC_CH      CH_EXTIN_6      /* PA3 = AIN6 */
#define BAT_ADC_GAIN    ADC_PGA_0       /* 0 dB single-ended range is 0..2.1 V */
#define BAT_ADC_FULL    4095u           /* 12-bit converter */
#define BAT_DIVIDER     2u              /* BT_VOL = VBAT / 2 */

#define BAT_POLL_MS     1000u            
#define BAT_VOL_DIV     1u             /* sample VBAT every 10 polls */
#define BAT_WINDOW_MS   1000u
#define BAT_FAULT_EDGES 2u              /* >= 2 CHG edges/s means a fault */
#define BAT_LOG_MS      5000u

/* The cable level is sampled on every pass and has to hold this long before it
 * is believed. Sampling it once a second let a marginal contact hold a wrong
 * level for a whole second, which the peripheral policy and usb_ready() both
 * act on. */
#define BAT_VBUS_DEBOUNCE_MS 50u

static uint8_t  s_pgood;
static uint8_t  s_pgood_raw;
static uint32_t s_pgood_raw_ms;
static uint8_t  s_charging;
static uint8_t  s_chg_prev;
static uint8_t  s_edge_count;
static uint8_t  s_fault;
static uint8_t  s_vol_div;
static uint16_t s_raw;
static uint16_t s_mv;
static int32_t  s_calib;
static uint32_t s_last_poll_ms;
static uint32_t s_window_ms;
static uint32_t s_log_ms;

/* Single-ended 0 dB: Vi = (ADC/2048) x Vref (Vref = VINTA ~= 1.05 V), so the
 * pin range is 0..2.1 V - a direct match for the 1:2 BT_VOL divider. */
static uint16_t bat_scale(uint16_t raw)
{
    int mv = ADC_VoltConverSignalPGA_0dB(raw);

    return (uint16_t)(mv * (int)BAT_DIVIDER);
}

static uint16_t bat_adc_sample(void)
{
    uint32_t acc = 0;

    for (uint8_t i = 0; i < 8u; i++) {
        int32_t v = (int32_t)ADC_ExcutSingleConver() + s_calib;
        if (v < 0) {
            v = 0;
        } else if (v > (int32_t)BAT_ADC_FULL) {
            v = (int32_t)BAT_ADC_FULL;
        }
        acc += (uint32_t)v;
    }

    return (uint16_t)(acc / 8u);
}

void bat_init(void)
{
    GPIOB_ModeCfg(BOARD_BAT_PGOOD_PIN | BOARD_BAT_CHG_PIN, GPIO_ModeIN_PU);
    s_pgood    = GPIOB_ReadPortPin(BOARD_BAT_PGOOD_PIN) ? 0u : 1u;
    s_charging = GPIOB_ReadPortPin(BOARD_BAT_CHG_PIN) ? 0u : 1u;
    s_chg_prev = s_charging;

    GPIOA_ModeCfg(BOARD_BAT_VOL_PIN, GPIO_ModeIN_Floating);
    GPIOADigitalCfg(DISABLE, BOARD_BAT_VOL_PIN);

    ADC_ChannelCfg(BAT_ADC_CH);
    ADC_ExtSingleChSampInit(SampleFreq_8_or_4, BAT_ADC_GAIN);
    s_calib = ADC_DataCalib_Rough();
    ADC_ExcutSingleConver();

    s_raw = bat_adc_sample();
    s_mv  = bat_scale(s_raw);

    LOG_I("BAT", "init pgood=%u chg=%u raw=%u vbat=%umV",
          (unsigned)s_pgood, (unsigned)s_charging, (unsigned)s_raw, (unsigned)s_mv);
}

void bat_poll(uint32_t now_ms)
{
    /* Ahead of the 1 s gate: this is the one signal the policy, the standby
     * guard and usb_ready() all hang off. */
    const uint8_t pgood = GPIOB_ReadPortPin(BOARD_BAT_PGOOD_PIN) ? 0u : 1u;

    if (pgood != s_pgood_raw) {
        s_pgood_raw    = pgood;
        s_pgood_raw_ms = now_ms;
    } else if ((pgood != s_pgood) && ((now_ms - s_pgood_raw_ms) >= BAT_VBUS_DEBOUNCE_MS)) {
        s_pgood = pgood;
        LOG_I("BAT", "power %s", pgood ? "present" : "absent");
    }

    if (now_ms - s_last_poll_ms < BAT_POLL_MS) {
        return;
    }
    s_last_poll_ms = now_ms;

    uint8_t chg = GPIOB_ReadPortPin(BOARD_BAT_CHG_PIN) ? 0u : 1u;
    if (chg != s_chg_prev) {
        s_chg_prev = chg;
        if (s_edge_count < 0xFFu) {
            s_edge_count++;
        }
    }

    if (now_ms - s_window_ms >= BAT_WINDOW_MS) {
        s_window_ms  = now_ms;
        s_fault      = (s_edge_count >= BAT_FAULT_EDGES) ? 1u : 0u;
        s_edge_count = 0u;
        if (s_fault) {
            LOG_W("BAT", "charge fault (CHG toggling)");
        }
    }

    if (!s_fault && chg != s_charging) {
        s_charging = chg;
        LOG_I("BAT", "charging %s", chg ? "started" : "stopped");
    }

    if (++s_vol_div >= BAT_VOL_DIV) {
        s_vol_div = 0u;
        s_raw = bat_adc_sample();
        s_mv  = bat_scale(s_raw);
    }

    if (now_ms - s_log_ms >= BAT_LOG_MS) {
        s_log_ms = now_ms;
        LOG_D("BAT", "vbat=%umV raw=%u pgood=%u chg=%u fault=%u",
              (unsigned)s_mv, (unsigned)s_raw,
              (unsigned)s_pgood, (unsigned)s_charging, (unsigned)s_fault);
    }
}

/* TEMPORARY BENCH OVERRIDE - remove before shipping.
 *
 * With this set the firmware always reports "no cable", so a board powered from
 * a USB current meter behaves exactly as it does on battery: the USBHS PHY
 * stays off, the panel / LED rail / BIO module stay off and the standby engages
 * after the idle timeout. Without it a USB-powered board reads the PHY and the
 * peripherals (tens of mA) instead of the sleep state.
 *
 * Side effects while set: charging is not detected and the USB host path never
 * comes up. Set to 0 to restore normal behaviour. */
#define BAT_BENCH_FAKE_UNPLUGGED 0

bool bat_power_good(void)      {
#if BAT_BENCH_FAKE_UNPLUGGED
    return false;
#else
    return s_pgood != 0u;
#endif
}
bool bat_charging(void)        { return s_charging != 0u; }
bool bat_charge_fault(void)    { return s_fault != 0u; }
uint16_t bat_voltage_mv(void)  { return s_mv; }
uint16_t bat_voltage_raw(void) { return s_raw; }
