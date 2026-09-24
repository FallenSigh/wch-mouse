#ifndef __BAT_H__
#define __BAT_H__

#include <stdbool.h>
#include <stdint.h>

/* BQ24075 open-drain status pins, both active low:
 *   PGOOD  PB6  -> low = a valid input source is connected
 *   CHG    PB5  -> low = charging; toggles ~2 Hz on a charge timer fault
 *   BT_VOL PA3  -> ADC AIN6, VBAT through a 1:2 divider (VBAT = 2 x V(PA3))
 */
void bat_init(void);
void bat_poll(uint32_t now_ms);

bool     bat_power_good(void);
bool     bat_charging(void);
bool     bat_charge_fault(void);
uint16_t bat_voltage_mv(void);
uint16_t bat_voltage_raw(void);

#endif
