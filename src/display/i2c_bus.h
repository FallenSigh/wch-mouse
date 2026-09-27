#ifndef __I2C_BUS_H__
#define __I2C_BUS_H__

#include <stdbool.h>
#include <stdint.h>

/* Blocking master I2C for the display bus, using the CH585 I2C peripheral.
 *
 * It runs on the remapped SCL_/SDA_ pins (PB21/PB20, RB_PIN_I2C) because the
 * default PB13/PB12 mapping is taken by this board's USB HS PHY. See
 * docs/io.csv for the wiring.
 *
 * Every wait is bounded, so a missing or wedged device returns false instead
 * of hanging the superloop. */
void i2c_bus_init(void);

/* True when addr7 acknowledges its address byte. */
bool i2c_bus_probe(uint8_t addr7);

/* Write len bytes to addr7. Returns false on a missing ACK or a timeout. */
bool i2c_bus_write(uint8_t addr7, const uint8_t *data, uint16_t len);

/* Same, but with a leading `reg` byte before the payload and no stop in
 * between - the shape display controllers expect for a control byte plus
 * data in one transaction. */
bool i2c_bus_write_reg(uint8_t addr7, uint8_t reg, const uint8_t *data, uint16_t len);

#endif /* __I2C_BUS_H__ */
