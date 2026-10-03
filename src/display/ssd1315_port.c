/*
 * src/display/ssd1315_port.c - LibDriver SSD1315 platform interface.
 *
 * LibDriver keeps the SSD1315 core portable by calling out through these
 * functions (see ssd1315/driver_ssd1315_interface.h); this file is the only
 * place that knows about the CH585. Commands and data both travel over the
 * shared I2C bus and are told apart by the SSD1315 control byte the driver
 * passes as `reg` (0x00 command, 0x40 data).
 *
 * The SPI entry points have to exist because the handle links them
 * unconditionally; this board is wired for I2C, so they are inert.
 */

#include "driver_ssd1315_interface.h"

#include <stdarg.h>
#include <stdio.h>

#include "CH58x_common.h"
#include "board.h"
#include "i2c_bus.h"
#include "log.h"

uint8_t ssd1315_interface_iic_init(void) {
    i2c_bus_init();
    return 0;
}

uint8_t ssd1315_interface_iic_deinit(void) {
    return 0;
}

uint8_t ssd1315_interface_iic_write(uint8_t addr, uint8_t reg, uint8_t *buf, uint16_t len) {
    /* LibDriver hands over the 8-bit write address (SSD1315_ADDR_SA0_0 is
     * 0x78), while i2c_bus takes a 7-bit one and shifts it itself - passing
     * 0x78 straight through would put 0xF0 on the wire and get NACKed. The
     * control byte and payload then go out in one transfer so a page write
     * stays a single I2C transaction. */
    return i2c_bus_write_reg((uint8_t)(addr >> 1), reg, buf, len) ? 0u : 1u;
}

uint8_t ssd1315_interface_spi_init(void) {
    return 0;
}

uint8_t ssd1315_interface_spi_deinit(void) {
    return 0;
}

uint8_t ssd1315_interface_spi_write_cmd(uint8_t *buf, uint16_t len) {
    (void)buf;
    (void)len;
    return 1;
}

uint8_t ssd1315_interface_spi_cmd_data_gpio_init(void) {
    return 0;
}

uint8_t ssd1315_interface_spi_cmd_data_gpio_deinit(void) {
    return 0;
}

uint8_t ssd1315_interface_spi_cmd_data_gpio_write(uint8_t value) {
    (void)value;
    return 1;
}

uint8_t ssd1315_interface_reset_gpio_init(void) {
    GPIOB_ModeCfg(BOARD_OLED_RST_PIN, GPIO_ModeOut_PP_5mA);
    return 0;
}

uint8_t ssd1315_interface_reset_gpio_deinit(void) {
    return 0;
}

uint8_t ssd1315_interface_reset_gpio_write(uint8_t value) {
    if (value != 0u) {
        GPIOB_SetBits(BOARD_OLED_RST_PIN);

        /* LibDriver releases reset and sends the first command immediately,
         * but the controller does not answer on I2C until its oscillator and
         * charge pump have settled - without this gap every command is NACKed
         * (AF) and the panel looks absent. */
        mDelaymS(120);
    } else {
        GPIOB_ResetBits(BOARD_OLED_RST_PIN);
    }

    return 0;
}

void ssd1315_interface_delay_ms(uint32_t ms) {
    mDelaymS(ms);
}

void ssd1315_interface_debug_print(const char *const fmt, ...) {
    char line[128];
    va_list args;
    int n;

    va_start(args, fmt);
    n = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    if (n > 0) {
        /* The library's own messages are failure reports, so surface them as
         * warnings rather than at debug level. */
        log_printf(LOG_LVL_WARN, "SSD1315", "%s", line);
    }
}
