#ifndef __OLED_H__
#define __OLED_H__

#include <stdbool.h>
#include <stdint.h>

/* SSD1315 128x64 panel on the shared I2C bus, driven by the vendored
 * LibDriver core in src/display/ssd1315/ (MIT). Pins per docs/io.csv:
 * SDA PB20, SCL PB21, RST PB16, panel rail enable PB3.
 *
 * Coordinates are display pixels: x 0..127, y 0..63. */
bool oled_init(void);

void oled_clear(void);
bool oled_flush(void);
bool oled_text(uint8_t x, uint8_t y, const char *str);
bool oled_fill(uint8_t left, uint8_t top, uint8_t right, uint8_t bottom, bool on);

/* Border plus text, to prove the gram maps onto the panel. */
void oled_demo(void);

#endif /* __OLED_H__ */
