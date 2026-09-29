/*
 * src/app/settings.c - persist the user configuration in the DataFlash.
 *
 * One 8-byte record in a 4 KiB DataFlash block. The ROM's EEPROM_* commands
 * address it by OFFSET (0 = 0x00070000), erase a whole 4 KiB block, and need a
 * RAM, 4-byte aligned buffer whose length is a multiple of 4 - the same shape
 * radio_mode.c uses. The offset is a different block from radio_mode's 0x0000
 * and clear of the vendor's 0x6000 / 0x7000, so a save here cannot wipe them.
 */

#include "settings.h"

#include "CH58x_common.h"
#include "bio.h"
#include "log.h"
#include "paw3395.h"
#include "paw3395_port.h"

#define SETTINGS_FLASH_OFF   0x1000u
#define SETTINGS_MAGIC       0xC5u

/* What a blank device boots with. */
#define SETTINGS_DEF_CPI          800u
#define SETTINGS_DEF_MODE         PAW3395_MODE_HIGH_PERFORMANCE
#define SETTINGS_DEF_LIFT         PAW3395_LIFT_CUT_2MM
#define SETTINGS_DEF_BIO_ACQUIRE  1u

/* Persist this long after the last change, so a burst of SETs costs one erase. */
#define SETTINGS_SAVE_DELAY_MS    1000u

/* Fixed on-flash layout: magic, the fields, padding to 8 bytes. */
typedef struct __attribute__((packed)) {
    uint8_t  magic;
    uint8_t  resv0;
    uint16_t cpi;
    uint8_t  sensor_mode;
    uint8_t  lift_cut;
    uint8_t  bio_acquire;
    uint8_t  resv1;
} settings_record_t;

static struct paw3395_dev *s_paw;

static settings_t s_cur = {
    .cpi          = SETTINGS_DEF_CPI,
    .sensor_mode  = SETTINGS_DEF_MODE,
    .lift_cut     = SETTINGS_DEF_LIFT,
    .bio_acquire  = SETTINGS_DEF_BIO_ACQUIRE,
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

    rec.magic       = SETTINGS_MAGIC;
    rec.resv0       = 0u;
    rec.cpi         = s_cur.cpi;
    rec.sensor_mode = s_cur.sensor_mode;
    rec.lift_cut    = s_cur.lift_cut;
    rec.bio_acquire = s_cur.bio_acquire;
    rec.resv1       = 0u;

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
    s_cur.cpi         = (rec.cpi >= 50u && rec.cpi <= 26000u) ? rec.cpi : SETTINGS_DEF_CPI;
    s_cur.sensor_mode = (rec.sensor_mode < PAW3395_MODE_COUNT) ? rec.sensor_mode : SETTINGS_DEF_MODE;
    s_cur.lift_cut    = (rec.lift_cut <= PAW3395_LIFT_CUT_2MM) ? rec.lift_cut : SETTINGS_DEF_LIFT;
    s_cur.bio_acquire = (rec.bio_acquire <= 1u) ? rec.bio_acquire : SETTINGS_DEF_BIO_ACQUIRE;

    LOG_I("SET", "loaded cpi=%u mode=%u lift=%u bio=%u", (unsigned)s_cur.cpi,
          (unsigned)s_cur.sensor_mode, (unsigned)s_cur.lift_cut, (unsigned)s_cur.bio_acquire);
}

const settings_t *settings_get(void)
{
    return &s_cur;
}

void settings_apply(void)
{
    /* Boot path: the caller runs it before the PA7 motion interrupt is armed,
     * so the blocking sensor writes cannot race the DMA burst. */
    paw3395_set_cpi(s_paw, s_cur.cpi);
    paw3395_set_mode(s_paw, (enum paw3395_mode)s_cur.sensor_mode);
    paw3395_set_lift_cut(s_paw, (enum paw3395_lift_cut)s_cur.lift_cut);

    if (s_cur.bio_acquire != 0u) {
        bio_measure_enable();
    } else {
        bio_measure_disable();
    }
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

    if (s_cur.bio_acquire != 0u) {
        bio_measure_enable();
    } else {
        bio_measure_disable();
    }

    settings_dirty();
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
