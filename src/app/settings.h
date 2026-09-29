#ifndef __SETTINGS_H__
#define __SETTINGS_H__

#include <stdbool.h>
#include <stdint.h>

struct paw3395_dev;

/* User configuration kept in DataFlash, so it survives a power cycle.
 * settings_init() loads the stored record (or falls back to the built-in
 * defaults) and settings_apply() writes the whole record to the hardware.
 * A field update applies immediately and marks the record dirty; settings_poll()
 * flushes it once the changes have settled. */
typedef struct __attribute__((packed)) {
    uint16_t cpi;
    uint8_t  sensor_mode;   /* enum paw3395_mode    */
    uint8_t  lift_cut;      /* enum paw3395_lift_cut */
    uint8_t  bio_acquire;   /* 0/1                  */
    uint8_t  rgb_enable;    /* 0/1                  */
    uint8_t  rgb_effect;    /* RGB_FX_EFFECT_*      */
    uint8_t  rgb_brightness;
    uint8_t  rgb_r;
    uint8_t  rgb_g;
    uint8_t  rgb_b;
    uint8_t  oled_enable;   /* 0/1                  */
    uint8_t  report_rate_idx;
    uint8_t  report_rate_rf_idx;
    uint8_t  report_rate_ble_idx;
} settings_t;

void settings_init(struct paw3395_dev *paw);
const settings_t *settings_get(void);

/* Write the whole record to the sensor and the BIO module (boot path). */
void settings_apply(void);

/* Apply the panel / LED rail / BIO module policy: all three only run while the
 * cable is in, because they are the biggest standing loads and tethered means
 * there is no battery to protect. Call at boot (before the USB device layer,
 * the panel bring-up blocks), on a VBUS change and on a config change. */
void settings_apply_peripherals(void);

/* Field updates: apply to the hardware now, persist after they settle. */
void settings_set_cpi(uint16_t cpi);
void settings_set_mode(uint8_t mode);
void settings_set_lift(uint8_t cut);
void settings_set_bio(uint8_t on);
void settings_set_rgb(uint8_t enable, uint8_t effect, uint8_t brightness,
                      uint8_t r, uint8_t g, uint8_t b);
void settings_set_oled(uint8_t on);

/* Report rate, one per link. Each is set and persisted independently, so
 * switching radio modes keeps every link's own rate. The unsuffixed pair
 * addresses the USB link and stays the default the host tools use. */
uint16_t settings_report_hz(void);
void     settings_set_report_hz(uint16_t hz);

uint16_t settings_report_hz_rf(void);
void     settings_set_report_hz_rf(uint16_t hz);

uint16_t settings_report_hz_ble(void);
void     settings_set_report_hz_ble(uint16_t hz);

/* Flush a dirty record to DataFlash. Call from the main loop. */
void settings_poll(uint32_t now_ms);

#endif /* __SETTINGS_H__ */
