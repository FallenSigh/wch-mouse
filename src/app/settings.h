#ifndef __SETTINGS_H__
#define __SETTINGS_H__

#include <stdbool.h>
#include <stdint.h>

struct paw3395_dev;

/* User configuration kept in DataFlash, so it survives a power cycle.
 * settings_init() loads the stored record (or falls back to the built-in
 * defaults) and settings_apply() writes the whole record to the hardware.
 * A field update applies immediately and marks the record dirty; settings_poll()
 * flushes it once the changes have settled.
 *
 * The storage layout is private - read fields through the accessors below. */
void settings_init(struct paw3395_dev *paw);

uint16_t settings_cpi(void);
uint8_t settings_mode(void);        /* enum paw3395_mode     */
uint8_t settings_lift(void);        /* enum paw3395_lift_cut */
uint8_t settings_oled_enable(void); /* 0/1                   */
uint8_t settings_periph_batt(void); /* 0/1                   */
/* Fills rgb[6] = { enable, effect, brightness, r, g, b }. */
void settings_rgb_get(uint8_t rgb[6]);

/* Write the whole record to the sensor and the BIO module (boot path). */
void settings_apply(void);

/* Apply the panel / LED rail / BIO module policy. Normally all three run only
 * while the cable is in, because they are the biggest standing loads and
 * tethered means there is no battery to protect; setting periph_batt lifts that
 * gate so the user's own oled/bio/rgb switches decide. Call at boot (before the
 * USB device layer, the panel bring-up blocks), on a VBUS change and on a
 * config change. */
void settings_apply_peripherals(void);

/* Field updates: apply to the hardware now, persist after they settle. */
void settings_set_cpi(uint16_t cpi);
void settings_set_mode(uint8_t mode);
void settings_set_lift(uint8_t cut);
void settings_set_bio(uint8_t on);
void settings_set_rgb(uint8_t enable, uint8_t effect, uint8_t brightness, uint8_t r, uint8_t g,
                      uint8_t b);
void settings_set_oled(uint8_t on);

/* Let the panel, the LED rail and the BIO module run on battery too. */
void settings_set_periph_batt(uint8_t on);

/* Report rate, one per link. Each is set and persisted independently, so
 * switching radio modes keeps every link's own rate. The unsuffixed pair
 * addresses the USB link and stays the default the host tools use. */
uint16_t settings_report_hz(void);
void settings_set_report_hz(uint16_t hz);

uint16_t settings_report_hz_rf(void);
void settings_set_report_hz_rf(uint16_t hz);

uint16_t settings_report_hz_ble(void);
void settings_set_report_hz_ble(uint16_t hz);

/* Air-mouse pointer sensitivity and IMU output-data-rate presets. Each is an
 * index into the tables owned by air_mouse.c. */
uint8_t settings_air_sens_idx(void);
void settings_set_air_sens(uint8_t idx);

uint8_t settings_air_odr_idx(void);
void settings_set_air_odr(uint8_t idx);

/* Vibration motor gate, persisted like the other peripheral switches. */
uint8_t settings_motor_enable(void);
void settings_set_motor_enable(uint8_t on);

/* Flush a dirty record to DataFlash. Call from the main loop. */
void settings_poll(uint32_t now_ms);

#endif /* __SETTINGS_H__ */
