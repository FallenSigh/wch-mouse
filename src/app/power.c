/*
 * src/app/power.c - desktop-idle standby.
 *
 * The mouse sits unused most of its life, so once it has been quiet on battery
 * for POWER_IDLE_MS the peripherals are parked and the MCU is put into its
 * low-power sleep (2.6 uA for the core + RAM32K; RAM96K is kept because this
 * image needs it).
 *
 * What wakes us, and why the sensor needs no configuration of its own:
 *
 *   - The PAW3395 keeps running through its own rest modes (Run -> Rest1 ->
 *     Rest2 -> Rest3 as it idles) and still drives MOTION. PA7 is the GPIO
 *     wake source; the response time is the sensor's own (about 200 ms from
 *     Rest2), which is the price of letting it drop to 11 uA.
 *   - BAT covers a charger or cable insert. That is also the escape hatch: a
 *     wrong wake configuration can never leave the board stranded, because
 *     plugging the cable always brings it back.
 *
 * The core power domain stays powered across LowPower_Sleep(), so the
 * peripheral registers survive; only the core clock, the flash and the
 * USB/BLE block go down. Anything derived from the core clock (TIM3, TIM2,
 * UART3) is re-armed by the caller when power_poll() reports a cycle.
 */

#include "power.h"

#include "CH58x_common.h"
#include "air_mouse.h"
#include "bat.h"
#include "bio.h"
#include "log.h"
#include "motor.h"
#include "mouse.h"
#include "oled.h"
#include "paw3395.h"
#include "paw3395_port.h"
#include "radio_mode.h"
#include "rgb.h"
#include "settings.h"
#include "transport.h"

#define POWER_IDLE_MS 30000u

/* Every peripheral clock except UART1, which stays on so the log works the
 * moment we are awake and costs nothing while the core is stopped. Must be
 * undone before ANY peripheral is touched on the way out: the sensor's SPI0,
 * the tick timers and the radio are all clocked from here. */
#define POWER_CLK_OFF_MASK ((uint16_t)(0xFFFFu & (uint32_t)~RB_SLP_CLK_UART1))

static struct paw3395_dev *s_paw;
static uint32_t            s_idle_from_ms;
static bool                s_tethered;

void power_init(struct paw3395_dev *paw)
{
    s_paw          = paw;
    s_idle_from_ms = 0u;
}

void power_note_activity(uint32_t now_ms)
{
    s_idle_from_ms = now_ms;
}

/* Everything that has to stop before the flash and the core clock go down. */
static void standby_park(void)
{
    /* The motion ISR runs with the flash still powered down, so it must not use
     * the SPI bus until the wake path has restored the clock. */
    paw3395_motion_park();

    /* Enforce what the rest of this path assumes instead of trusting the
     * peripheral gate: a 2.6 uA sleep must not be spent with the panel lit, the
     * rail driving the LEDs or the motor running. The motor is the one that
     * really needs this - its own timing lives in motor_poll(), which the main
     * loop cannot run once we are asleep - and all of it has to happen while the
     * clocks, the I2C and the UART are still up. */
    motor_off();
    rgb_set_enable(false);
    (void)oled_set_enable(false);
    bio_measure_disable();
    bio_sleep_enable();

    /* What is left to give up here is the ADC reference and the clocks. */
    R8_ADC_CFG &= (uint8_t)~(RB_ADC_POWER_ON | RB_ADC_BUF_EN);

    PWR_PeriphClkCfg(DISABLE, POWER_CLK_OFF_MASK);
    PWR_SafeClkCfg(DISABLE, 0x7);
}

/* PA7 is already an armed falling-edge interrupt; the button pins are armed
 * here. GPIO wake turns either into a sleep wake-up. BAT is the charger/cable
 * escape hatch. */
static void standby_wake_cfg(void)
{
    mouse_wake_arm();
    PWR_PeriphWakeUpCfg(ENABLE, RB_SLP_GPIO_WAKE, Long_Delay);
    PWR_PeriphWakeUpCfg(ENABLE, RB_SLP_BAT_WAKE, Long_Delay);
}

/* Runs from RAM: LowPower_Sleep() powers the flash down and switches the system
 * clock, so nothing on this path may execute from flash once it is entered. */
__HIGH_CODE
static void standby_sleep(void)
{
    uint32_t irq_status;
    uint8_t  wake_ctrl;
    volatile uint32_t spin;

    SYS_DisableAllIrq(&irq_status);
    wake_ctrl = R8_SLP_WAKE_CTRL;

    /* SYS_DisableAllIrq() clears EVERY PFIC enable, the motion pin's and the
     * buttons' included, and a PMU wake source whose interrupt is disabled
     * never fires. Without this the mouse sleeps and never wakes on input -
     * put those interrupts straight back. */
    PFIC_EnableIRQ(GPIO_A_IRQn);
    PFIC_EnableIRQ(GPIO_B_IRQn);

    /* No RTC wake: the sensor, the buttons and the cable are the only things
     * that should bring us back, and keeping the 32K alive for nothing costs
     * current. */
    sys_safe_access_enable();
    R8_SLP_WAKE_CTRL = RB_WAKE_EV_MODE | RB_SLP_GPIO_WAKE | RB_SLP_BAT_WAKE;
    sys_safe_access_disable();

    LowPower_Sleep(RB_PWR_RAM32K | RB_PWR_RAM96K);

    /* The flash is powered again but needs a few hundred microseconds before it
     * can be read (LowPower_Sleep() leaves that delay to its caller, with the
     * vendor's own DelayUs(300) commented out). This function runs from RAM, so
     * spin here rather than let the next flash fetch come back as garbage. */
    for (spin = 0u; spin < 60000u; spin++) {
        __nop();
    }

    SetSysClock(SYSCLK_FREQ);
    HSECFG_Current(HSE_RCur_100);

    sys_safe_access_enable();
    R8_SLP_WAKE_CTRL = wake_ctrl;
    sys_safe_access_disable();

    SYS_RecoverIrq(irq_status);
}

static void standby_restore(void)
{
    /* Un-gate the peripheral clocks before anything touches a peripheral: the
     * radio and the sensor's SPI0 both hang off this mask. */
    PWR_PeriphClkCfg(ENABLE, POWER_CLK_OFF_MASK);
    PWR_SafeClkCfg(ENABLE, 0x7);

    /* Rebuild the radio first: the sleep cut its power domain and SYS_RecoverIrq
     * has just re-enabled its interrupts, while the rest of this function blocks
     * for a while. */
    transport_router_init();

    /* The motion interrupt is still parked, so the sensor writes in here cannot
     * collide with a burst on the shared SPI bus. */
    settings_apply();

    /* The park cleared the ADC's power bit, so the battery gauge has to be
     * re-armed before bat_poll() samples it again. */
    bat_init();

    /* Unparks the ISR and drains anything the wake held back. */
    paw3395_motion_start();
    mouse_wake_disarm();
}

bool power_poll(uint32_t now_ms)
{
    if (s_paw == NULL) {
        return false;
    }

    /* The panel, the LED rail and the BIO module follow the cable: apply the
     * policy on every change so plugging in brings them up and unplugging takes
     * them straight back down. */
    const bool tethered = bat_power_good();

    if (tethered != s_tethered) {
        s_tethered = tethered;
        settings_apply_peripherals();
    }

    /* A cable means charging, and a charger looks exactly like a host - a user
     * who just plugged in expects the link to stay up. */
    if (tethered) {
        power_note_activity(now_ms);
        return false;
    }

    /* Air-mouse mode stops the PA7 interrupt and reads the gyro instead, so
     * there would be nothing left to wake on. */
    if (air_mouse_active()) {
        power_note_activity(now_ms);
        return false;
    }

    /* The peripherals policy can keep the panel, the rail and the BIO module
     * live on battery, and standby_park() assumes they are already dark - do
     * not sleep with them up. */
    if (settings_periph_on_battery() && settings_periph_any_enabled()) {
        power_note_activity(now_ms);
        return false;
    }

    const bool idle = ((now_ms - s_idle_from_ms) >= POWER_IDLE_MS);

    /* BLE has no standby: it cannot deep-sleep (the link is kept alive by
     * connection events every 7.5-10 ms and the stack cannot be rebuilt from
     * here), and with the panel, the LED rail and the BIO module dropped there
     * is nothing left for it to park either. */
    if (!radio_mode_is(RADIO_MODE_RF)) {
        power_note_activity(now_ms);
        return false;
    }

    if (!idle) {
        return false;
    }

    LOG_I("PWR", "standby: parking, sleeping");

    standby_park();
    standby_wake_cfg();
    standby_sleep();
    standby_restore();

    LOG_I("PWR", "standby: awake");

    /* TIM3 stopped with the core, so now_ms did not advance while we slept;
     * re-base the idle timer or the next pass would sleep again immediately. */
    s_idle_from_ms = now_ms;

    return true;
}
