/*
 * src/app/settings.c - persist the user configuration in the DataFlash.
 *
 * One 16-byte record in a 4 KiB DataFlash block. The ROM's EEPROM_* commands
 * address it by OFFSET (0 = 0x00070000), erase a whole 4 KiB block, and need a
 * RAM, 4-byte aligned buffer whose length is a multiple of 4 - the same shape
 * radio_mode.c uses. The offset is a different block from radio_mode's 0x0000
 * and clear of the vendor's 0x6000 / 0x7000, so a save here cannot wipe them.
 */

#include "settings.h"

#include "CH58x_common.h"
#include "bat.h"
#include "bio.h"
#include "log.h"
#include "oled.h"
#include "paw3395.h"
#include "paw3395_port.h"
#include "rgb_fx.h"

#define SETTINGS_FLASH_OFF   0x1000u
#define SETTINGS_MAGIC       0xC7u

/* What a blank device boots with. */
#define SETTINGS_DEF_CPI          800u
#define SETTINGS_DEF_MODE         PAW3395_MODE_HIGH_PERFORMANCE
#define SETTINGS_DEF_LIFT         PAW3395_LIFT_CUT_2MM
/* The panel, the LED rail and the BIO module are the standing mA-level loads,
 * so a blank device boots with all three off; they are only brought up on
 * request, and only while tethered (settings_apply_peripherals()). */
#define SETTINGS_DEF_BIO_ACQUIRE  0u
#define SETTINGS_DEF_RGB_ENABLE   0u
#define SETTINGS_DEF_RGB_EFFECT   RGB_FX_EFFECT_HUE
#define SETTINGS_DEF_RGB_BRIGHT   1u
#define SETTINGS_DEF_RGB_R        255u
#define SETTINGS_DEF_RGB_G        255u
#define SETTINGS_DEF_RGB_B        255u
#define SETTINGS_DEF_OLED_ENABLE  0u
#define SETTINGS_DEF_RATE_IDX     0u   /* USB: 1000 Hz */
#define SETTINGS_DEF_RATE_RF_IDX  0u   /* RF:  1000 Hz */
#define SETTINGS_DEF_RATE_BLE_IDX 3u   /* BLE:  500 Hz (index 3 of s_rate_hz) */

/* Supported report rates, in Hz. Index 0 is both the default and what a record
 * written before this setting existed reads (its reserved bytes are 0), so
 * older records keep 1 kHz on every link. Every entry divides TICK_HZ. */
static const uint16_t s_rate_hz[] = { 1000u, 125u, 250u, 500u, 2000u, 4000u, 8000u };
#define RATE_COUNT ((uint8_t)(sizeof(s_rate_hz) / sizeof(s_rate_hz[0])))

/* Persist this long after the last change, so a burst of SETs costs one erase. */
#define SETTINGS_SAVE_DELAY_MS    1000u

/* Peripherals may be asked to run on battery too. Off by default: they are the
 * biggest standing loads and the cable is what normally justifies them. */
#define SETTINGS_DEF_PERIPH_BATT  0u

/* Fixed on-flash layout. The per-link report rates reuse the two reserved bytes
 * and periph_batt is appended, so a record written before either existed still
 * lines up field for field: those bytes read back as 0xFF from the erase and the
 * per-field checks below fall back to the defaults. */
typedef struct __attribute__((packed)) {
    uint8_t  magic;
    uint8_t  report_rate_rf_idx;
    uint16_t cpi;
    uint8_t  sensor_mode;
    uint8_t  lift_cut;
    uint8_t  bio_acquire;
    uint8_t  rgb_enable;
    uint8_t  rgb_effect;
    uint8_t  rgb_brightness;
    uint8_t  rgb_r;
    uint8_t  rgb_g;
    uint8_t  rgb_b;
    uint8_t  oled_enable;
    uint8_t  report_rate_idx;
    uint8_t  report_rate_ble_idx;
    uint8_t  periph_batt;
} settings_record_t;

static struct paw3395_dev *s_paw;

static settings_t s_cur = {
    .cpi            = SETTINGS_DEF_CPI,
    .sensor_mode    = SETTINGS_DEF_MODE,
    .lift_cut       = SETTINGS_DEF_LIFT,
    .bio_acquire    = SETTINGS_DEF_BIO_ACQUIRE,
    .rgb_enable     = SETTINGS_DEF_RGB_ENABLE,
    .rgb_effect     = SETTINGS_DEF_RGB_EFFECT,
    .rgb_brightness = SETTINGS_DEF_RGB_BRIGHT,
    .rgb_r          = SETTINGS_DEF_RGB_R,
    .rgb_g          = SETTINGS_DEF_RGB_G,
    .rgb_b          = SETTINGS_DEF_RGB_B,
    .oled_enable    = SETTINGS_DEF_OLED_ENABLE,
    .report_rate_idx     = SETTINGS_DEF_RATE_IDX,
    .report_rate_rf_idx  = SETTINGS_DEF_RATE_RF_IDX,
    .report_rate_ble_idx = SETTINGS_DEF_RATE_BLE_IDX,
    .periph_batt         = SETTINGS_DEF_PERIPH_BATT,
};

static bool     s_dirty;
static uint32_t s_save_at;

static void settings_dirty(void)
{
    s_dirty   = true;
    s_save_at = 0u;   /* settings_poll() starts the delay on its next pass */
}

static void settings_save(void)
{
    __attribute__((aligned(4))) settings_record_t rec;
    uint32_t er;
    uint32_t wr;

    rec.magic          = SETTINGS_MAGIC;
    rec.report_rate_rf_idx = s_cur.report_rate_rf_idx;
    rec.cpi            = s_cur.cpi;
    rec.sensor_mode    = s_cur.sensor_mode;
    rec.lift_cut       = s_cur.lift_cut;
    rec.bio_acquire    = s_cur.bio_acquire;
    rec.rgb_enable     = s_cur.rgb_enable;
    rec.rgb_effect     = s_cur.rgb_effect;
    rec.rgb_brightness = s_cur.rgb_brightness;
    rec.rgb_r          = s_cur.rgb_r;
    rec.rgb_g          = s_cur.rgb_g;
    rec.rgb_b          = s_cur.rgb_b;
    rec.oled_enable    = s_cur.oled_enable;
    rec.report_rate_idx = s_cur.report_rate_idx;
    rec.report_rate_ble_idx = s_cur.report_rate_ble_idx;
    rec.periph_batt    = s_cur.periph_batt;

    er = EEPROM_ERASE(SETTINGS_FLASH_OFF, EEPROM_BLOCK_SIZE);
    wr = EEPROM_WRITE(SETTINGS_FLASH_OFF, (uint8_t *)&rec, sizeof(rec));

    LOG_I("SET", "save cpi=%u mode=%u lift=%u bio=%u er=%u wr=%u",
          (unsigned)s_cur.cpi, (unsigned)s_cur.sensor_mode, (unsigned)s_cur.lift_cut,
          (unsigned)s_cur.bio_acquire, (unsigned)er, (unsigned)wr);
}

void settings_init(struct paw3395_dev *paw)
{
    __attribute__((aligned(4))) settings_record_t rec;

    s_paw   = paw;
    s_dirty = false;

    EEPROM_READ(SETTINGS_FLASH_OFF, (uint8_t *)&rec, sizeof(rec));

    if (rec.magic != SETTINGS_MAGIC) {
        LOG_I("SET", "defaults cpi=%u mode=%u lift=%u bio=%u", (unsigned)s_cur.cpi,
              (unsigned)s_cur.sensor_mode, (unsigned)s_cur.lift_cut,
              (unsigned)s_cur.bio_acquire);
        return;
    }

    /* Reject a stale or foreign record field by field. */
    s_cur.cpi            = (rec.cpi >= 50u && rec.cpi <= 26000u) ? rec.cpi : SETTINGS_DEF_CPI;
    s_cur.sensor_mode    = (rec.sensor_mode < PAW3395_MODE_COUNT) ? rec.sensor_mode : SETTINGS_DEF_MODE;
    s_cur.lift_cut       = (rec.lift_cut <= PAW3395_LIFT_CUT_2MM) ? rec.lift_cut : SETTINGS_DEF_LIFT;
    s_cur.bio_acquire    = (rec.bio_acquire <= 1u) ? rec.bio_acquire : SETTINGS_DEF_BIO_ACQUIRE;
    s_cur.rgb_enable     = (rec.rgb_enable <= 1u) ? rec.rgb_enable : SETTINGS_DEF_RGB_ENABLE;
    s_cur.rgb_effect     = (rec.rgb_effect <= RGB_FX_EFFECT_SOLID) ? rec.rgb_effect : SETTINGS_DEF_RGB_EFFECT;
    s_cur.rgb_brightness = (rec.rgb_brightness != 0u) ? rec.rgb_brightness : SETTINGS_DEF_RGB_BRIGHT;
    s_cur.rgb_r          = rec.rgb_r;
    s_cur.rgb_g          = rec.rgb_g;
    s_cur.rgb_b          = rec.rgb_b;
    s_cur.oled_enable    = (rec.oled_enable <= 1u) ? rec.oled_enable : SETTINGS_DEF_OLED_ENABLE;
    s_cur.report_rate_idx     = (rec.report_rate_idx < RATE_COUNT) ? rec.report_rate_idx : SETTINGS_DEF_RATE_IDX;
    s_cur.report_rate_rf_idx  = (rec.report_rate_rf_idx < RATE_COUNT) ? rec.report_rate_rf_idx : SETTINGS_DEF_RATE_RF_IDX;
    s_cur.report_rate_ble_idx = (rec.report_rate_ble_idx < RATE_COUNT) ? rec.report_rate_ble_idx : SETTINGS_DEF_RATE_BLE_IDX;
    s_cur.periph_batt    = (rec.periph_batt <= 1u) ? rec.periph_batt : SETTINGS_DEF_PERIPH_BATT;

    LOG_I("SET", "loaded cpi=%u mode=%u lift=%u bio=%u rgb=%u/%u/%u oled=%u rate=%u", (unsigned)s_cur.cpi,
          (unsigned)s_cur.sensor_mode, (unsigned)s_cur.lift_cut, (unsigned)s_cur.bio_acquire,
          (unsigned)s_cur.rgb_enable, (unsigned)s_cur.rgb_effect, (unsigned)s_cur.rgb_brightness,
          (unsigned)s_cur.oled_enable, (unsigned)s_rate_hz[s_cur.report_rate_idx]);
}

const settings_t *settings_get(void)
{
    return &s_cur;
}

/* The panel, the rail and the BIO module run off the cable, or on battery when
 * the user has overridden the policy. Both the RGB config and the peripheral
 * policy ask this, so they cannot disagree about what "on" means. */
static bool settings_periph_powered(void)
{
    return bat_power_good() || (s_cur.periph_batt != 0u);
}

static void settings_apply_rgb(void)
{
    const rgb_fx_cfg_t cfg = {
        /* `enable` also carries the battery gate, which drops the idle
         * animation; `transient` follows the user's switch alone so a mode
         * switch still flashes while untethered. */
        .enable     = (s_cur.rgb_enable != 0u) && settings_periph_powered(),
        .transient  = (s_cur.rgb_enable != 0u),
        .effect     = s_cur.rgb_effect,
        .brightness = s_cur.rgb_brightness,
        .r          = s_cur.rgb_r,
        .g          = s_cur.rgb_g,
        .b          = s_cur.rgb_b,
    };

    rgb_fx_set_config(&cfg);
}

void settings_apply(void)
{
    /* Boot path: the caller runs it before the PA7 motion interrupt is armed,
     * so the blocking sensor writes cannot race the DMA burst. */
    paw3395_set_cpi(s_paw, s_cur.cpi);
    paw3395_set_mode(s_paw, (enum paw3395_mode)s_cur.sensor_mode);
    paw3395_set_lift_cut(s_paw, (enum paw3395_lift_cut)s_cur.lift_cut);
}

void settings_apply_peripherals(void)
{
    const bool powered = settings_periph_powered();

    if ((s_cur.oled_enable != 0u) && powered) {
        (void)oled_set_enable(true);
    } else {
        (void)oled_set_enable(false);
    }

    if ((s_cur.bio_acquire != 0u) && powered) {
        /* Two passes. Measured on hardware: the module drops the first command
         * pair after a wake - starting it used to take two host commands - and
         * there is no acknowledgement to retry against, so the pair is simply
         * sent twice. */
        bio_sleep_disable();
        mDelaymS(BIO_WAKE_DELAY_MS);
        bio_measure_enable();
    } else {
        /* The sleep command is enough on its own: stopping acquisition or the
         * checkup first is not required (measured on hardware). */
        bio_sleep_enable();
    }

    settings_apply_rgb();
}

/* The sensor writes share SPI0 with the PA7 motion ISR, so park it around them. */
void settings_set_cpi(uint16_t cpi)
{
    paw3395_motion_stop();
    paw3395_set_cpi(s_paw, cpi);
    paw3395_motion_start();

    s_cur.cpi = s_paw->cpi;   /* the driver clamps */
    settings_dirty();
}

void settings_set_mode(uint8_t mode)
{
    if (mode >= (uint8_t)PAW3395_MODE_COUNT) {
        return;
    }

    paw3395_motion_stop();
    paw3395_set_mode(s_paw, (enum paw3395_mode)mode);
    paw3395_motion_start();

    s_cur.sensor_mode = (uint8_t)s_paw->mode;
    settings_dirty();
}

void settings_set_lift(uint8_t cut)
{
    if (cut > (uint8_t)PAW3395_LIFT_CUT_2MM) {
        return;
    }

    paw3395_motion_stop();
    paw3395_set_lift_cut(s_paw, (enum paw3395_lift_cut)cut);
    paw3395_motion_start();

    s_cur.lift_cut = (uint8_t)s_paw->lift_cut;
    settings_dirty();
}

void settings_set_bio(uint8_t on)
{
    s_cur.bio_acquire = (on != 0u) ? 1u : 0u;
    settings_apply_peripherals();
    settings_dirty();
}

void settings_set_rgb(uint8_t enable, uint8_t effect, uint8_t brightness,
                      uint8_t r, uint8_t g, uint8_t b)
{
    s_cur.rgb_enable     = (enable != 0u) ? 1u : 0u;
    s_cur.rgb_effect     = (effect <= RGB_FX_EFFECT_SOLID) ? effect : SETTINGS_DEF_RGB_EFFECT;
    s_cur.rgb_brightness = brightness;
    s_cur.rgb_r          = r;
    s_cur.rgb_g          = g;
    s_cur.rgb_b          = b;

    settings_apply_rgb();
    settings_dirty();
}

void settings_set_oled(uint8_t on)
{
    s_cur.oled_enable = (on != 0u) ? 1u : 0u;
    settings_apply_peripherals();
    settings_dirty();
}

void settings_set_periph_batt(uint8_t on)
{
    s_cur.periph_batt = (on != 0u) ? 1u : 0u;
    settings_apply_peripherals();
    settings_dirty();
}

bool settings_periph_on_battery(void)
{
    return s_cur.periph_batt != 0u;
}

bool settings_periph_any_enabled(void)
{
    return (s_cur.oled_enable != 0u) || (s_cur.bio_acquire != 0u) || (s_cur.rgb_enable != 0u);
}

/* Pick the supported rate closest to the request, so the host can send any Hz. */
static uint8_t settings_rate_idx_for(uint16_t hz)
{
    uint8_t  best   = 0u;
    uint32_t best_d = 0xFFFFFFFFu;

    for (uint8_t i = 0u; i < RATE_COUNT; i++) {
        const uint32_t d = (s_rate_hz[i] > hz) ? (uint32_t)(s_rate_hz[i] - hz)
                                              : (uint32_t)(hz - s_rate_hz[i]);

        if (d < best_d) {
            best_d = d;
            best   = i;
        }
    }

    return best;
}

/* One accessor pair per link, over the shared index table. */
static uint16_t settings_rate_hz_of(uint8_t idx)
{
    return s_rate_hz[(idx < RATE_COUNT) ? idx : 0u];
}

static void settings_rate_set(uint8_t *idx, uint16_t hz)
{
    const uint8_t want = settings_rate_idx_for(hz);

    if (want == *idx) {
        return;
    }

    *idx = want;
    settings_dirty();
}

uint16_t settings_report_hz(void)
{
    return settings_rate_hz_of(s_cur.report_rate_idx);
}

void settings_set_report_hz(uint16_t hz)
{
    settings_rate_set(&s_cur.report_rate_idx, hz);
}

uint16_t settings_report_hz_rf(void)
{
    return settings_rate_hz_of(s_cur.report_rate_rf_idx);
}

void settings_set_report_hz_rf(uint16_t hz)
{
    settings_rate_set(&s_cur.report_rate_rf_idx, hz);
}

uint16_t settings_report_hz_ble(void)
{
    return settings_rate_hz_of(s_cur.report_rate_ble_idx);
}

void settings_set_report_hz_ble(uint16_t hz)
{
    settings_rate_set(&s_cur.report_rate_ble_idx, hz);
}

void settings_poll(uint32_t now_ms)
{
    if (!s_dirty) {
        return;
    }

    if (s_save_at == 0u) {
        s_save_at = now_ms + SETTINGS_SAVE_DELAY_MS;
        return;
    }

    if ((int32_t)(now_ms - s_save_at) >= 0) {
        settings_save();
        s_dirty   = false;
        s_save_at = 0u;
    }
}
