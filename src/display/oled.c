/*
 * src/display/oled.c - SSD1315 128x64 panel on the shared I2C bus.
 *
 * Glue over the vendored LibDriver core (src/display/ssd1315/, MIT): it owns
 * the handle, powers the panel, runs the bring-up sequence and exposes the
 * small drawing API the rest of the firmware uses. Panel parameters mirror
 * LibDriver's own basic example for a 128x64 module.
 */

#include "oled.h"

#include "CH58x_common.h"
#include "driver_ssd1315.h"
#include "driver_ssd1315_interface.h"
#include "i2c_bus.h"
#include "log.h"

#define OLED_EN_PIN         GPIO_Pin_3            /* PB3,  panel rail switch */
#define OLED_RST_PIN        GPIO_Pin_16           /* PB16, active low        */
#define OLED_IIC_ADDR       SSD1315_ADDR_SA0_0    /* SA0 low -> write 0x3C   */
#define OLED_IIC_ADDR_7BIT  0x3Cu                 /* the 7-bit form our bus takes */

#define OLED_WIDTH          128u
#define OLED_HEIGHT         64u
#define OLED_PAGES          (OLED_HEIGHT / 8u)

/* Page-addressing commands and the control bytes that frame them. LibDriver
 * keeps its copies private, but the flush below drives the panel directly so
 * a page leaves in one I2C transaction instead of 128. */
#define SSD1315_CTRL_CMD    0x00u
#define SSD1315_CTRL_DATA   0x40u
#define SSD1315_CMD_PAGE    0xB0u
#define SSD1315_CMD_COL_LOW 0x00u
#define SSD1315_CMD_COL_HIGH 0x10u

static ssd1315_handle_t s_oled;
static bool             s_ready;

#define OLED_TRY(call)                                        \
    do {                                                      \
        if ((call) != 0u) {                                   \
            LOG_E("OLED", "setup failed: %s", #call);         \
            return false;                                     \
        }                                                     \
    } while (0)

static void oled_rail(bool on)
{
    GPIOB_ModeCfg(OLED_EN_PIN, GPIO_ModeOut_PP_5mA);

    if (on) {
        GPIOB_SetBits(OLED_EN_PIN);
    } else {
        GPIOB_ResetBits(OLED_EN_PIN);
    }
}

static void oled_reset(void)
{
    GPIOB_ModeCfg(OLED_RST_PIN, GPIO_ModeOut_PP_5mA);

    GPIOB_ResetBits(OLED_RST_PIN);   /* assert  */
    mDelaymS(20);
    GPIOB_SetBits(OLED_RST_PIN);     /* release */
    mDelaymS(20);
}

bool oled_init(void)
{
    oled_rail(true);
    oled_reset();

    i2c_bus_init();
    LOG_I("OLED", "panel at 0x%02X %s", 0x3Cu, i2c_bus_probe(0x3Cu) ? "ACKs" : "does NOT ACK");

    DRIVER_SSD1315_LINK_INIT(&s_oled, ssd1315_handle_t);
    DRIVER_SSD1315_LINK_IIC_INIT(&s_oled, ssd1315_interface_iic_init);
    DRIVER_SSD1315_LINK_IIC_DEINIT(&s_oled, ssd1315_interface_iic_deinit);
    DRIVER_SSD1315_LINK_IIC_WRITE(&s_oled, ssd1315_interface_iic_write);
    DRIVER_SSD1315_LINK_SPI_INIT(&s_oled, ssd1315_interface_spi_init);
    DRIVER_SSD1315_LINK_SPI_DEINIT(&s_oled, ssd1315_interface_spi_deinit);
    DRIVER_SSD1315_LINK_SPI_WRITE_COMMAND(&s_oled, ssd1315_interface_spi_write_cmd);
    DRIVER_SSD1315_LINK_SPI_COMMAND_DATA_GPIO_INIT(&s_oled, ssd1315_interface_spi_cmd_data_gpio_init);
    DRIVER_SSD1315_LINK_SPI_COMMAND_DATA_GPIO_DEINIT(&s_oled, ssd1315_interface_spi_cmd_data_gpio_deinit);
    DRIVER_SSD1315_LINK_SPI_COMMAND_DATA_GPIO_WRITE(&s_oled, ssd1315_interface_spi_cmd_data_gpio_write);
    DRIVER_SSD1315_LINK_RESET_GPIO_INIT(&s_oled, ssd1315_interface_reset_gpio_init);
    DRIVER_SSD1315_LINK_RESET_GPIO_DEINIT(&s_oled, ssd1315_interface_reset_gpio_deinit);
    DRIVER_SSD1315_LINK_RESET_GPIO_WRITE(&s_oled, ssd1315_interface_reset_gpio_write);
    DRIVER_SSD1315_LINK_DELAY_MS(&s_oled, ssd1315_interface_delay_ms);
    DRIVER_SSD1315_LINK_DEBUG_PRINT(&s_oled, ssd1315_interface_debug_print);

    OLED_TRY(ssd1315_set_interface(&s_oled, SSD1315_INTERFACE_IIC));
    OLED_TRY(ssd1315_set_addr_pin(&s_oled, OLED_IIC_ADDR));
    OLED_TRY(ssd1315_init(&s_oled));

    OLED_TRY(ssd1315_set_display(&s_oled, SSD1315_DISPLAY_OFF));
    OLED_TRY(ssd1315_set_column_address_range(&s_oled, 0x00, 0x7F));
    OLED_TRY(ssd1315_set_page_address_range(&s_oled, 0x00, 0x07));
    OLED_TRY(ssd1315_set_low_column_start_address(&s_oled, 0x00));
    OLED_TRY(ssd1315_set_high_column_start_address(&s_oled, 0x00));
    OLED_TRY(ssd1315_set_display_start_line(&s_oled, 0x00));
    OLED_TRY(ssd1315_set_fade_blinking_mode(&s_oled, SSD1315_FADE_BLINKING_MODE_DISABLE, 0x00));
    OLED_TRY(ssd1315_deactivate_scroll(&s_oled));
    OLED_TRY(ssd1315_set_zoom_in(&s_oled, SSD1315_ZOOM_IN_DISABLE));
    OLED_TRY(ssd1315_set_contrast(&s_oled, 0xCF));
    OLED_TRY(ssd1315_set_segment_remap(&s_oled, SSD1315_SEGMENT_COLUMN_ADDRESS_127));
    OLED_TRY(ssd1315_set_scan_direction(&s_oled, SSD1315_SCAN_DIRECTION_COMN_1_START));
    OLED_TRY(ssd1315_set_display_mode(&s_oled, SSD1315_DISPLAY_MODE_NORMAL));
    OLED_TRY(ssd1315_set_multiplex_ratio(&s_oled, 0x3F));
    OLED_TRY(ssd1315_set_display_offset(&s_oled, 0x00));
    OLED_TRY(ssd1315_set_display_clock(&s_oled, 0x08, 0x00));
    OLED_TRY(ssd1315_set_precharge_period(&s_oled, 0x01, 0x0F));
    OLED_TRY(ssd1315_set_iref(&s_oled, SSD1315_IREF_ENABLE, SSD1315_IREF_VALUE_19UA_150UA));
    OLED_TRY(ssd1315_set_com_pins_hardware_conf(&s_oled, SSD1315_PIN_CONF_ALTERNATIVE,
                                                SSD1315_LEFT_RIGHT_REMAP_DISABLE));
    OLED_TRY(ssd1315_set_deselect_level(&s_oled, SSD1315_DESELECT_LEVEL_0P77));
    OLED_TRY(ssd1315_set_memory_addressing_mode(&s_oled, SSD1315_MEMORY_ADDRESSING_MODE_PAGE));
    OLED_TRY(ssd1315_set_charge_pump(&s_oled, SSD1315_CHARGE_PUMP_ENABLE,
                                     SSD1315_CHARGE_PUMP_MODE_7P5V));
    OLED_TRY(ssd1315_set_entire_display(&s_oled, SSD1315_ENTIRE_DISPLAY_OFF));
    OLED_TRY(ssd1315_set_display(&s_oled, SSD1315_DISPLAY_ON));

    s_ready = true;
    oled_clear();
    if (!oled_flush()) {
        LOG_E("OLED", "first flush failed");
        s_ready = false;
        return false;
    }

    LOG_I("OLED", "ssd1315 %ux%u up", (unsigned)OLED_WIDTH, (unsigned)OLED_HEIGHT);
    return true;
}

void oled_clear(void)
{
    if (s_ready) {
        (void)ssd1315_gram_fill_rect(&s_oled, 0u, 0u, OLED_WIDTH - 1u, OLED_HEIGHT - 1u, 0u);
    }
}

static bool oled_cmd(uint8_t cmd)
{
    return i2c_bus_write_reg(OLED_IIC_ADDR_7BIT, SSD1315_CTRL_CMD, &cmd, 1u);
}

bool oled_flush(void)
{
    uint8_t row[OLED_WIDTH];

    if (!s_ready) {
        return false;
    }

    for (uint8_t page = 0u; page < OLED_PAGES; page++) {
        if (!oled_cmd(SSD1315_CMD_PAGE | page) || !oled_cmd(SSD1315_CMD_COL_LOW) ||
            !oled_cmd(SSD1315_CMD_COL_HIGH)) {
            return false;
        }

        /* LibDriver stores the gram column-major (gram[x][page]), so gather a
         * page into a row buffer to send it as one transfer. */
        for (uint8_t x = 0u; x < OLED_WIDTH; x++) {
            row[x] = s_oled.gram[x][page];
        }

        if (!i2c_bus_write_reg(OLED_IIC_ADDR_7BIT, SSD1315_CTRL_DATA, row, OLED_WIDTH)) {
            return false;
        }
    }

    return true;
}

bool oled_text(uint8_t x, uint8_t y, const char *str)
{
    uint16_t len = 0u;

    while ((str != NULL) && (str[len] != '\0')) {
        len++;
    }

    if (!s_ready || (len == 0u)) {
        return false;
    }

    return ssd1315_gram_write_string(&s_oled, x, y, (char *)str, len, 1u, SSD1315_FONT_16) == 0u;
}

bool oled_fill(uint8_t left, uint8_t top, uint8_t right, uint8_t bottom, bool on)
{
    if (!s_ready) {
        return false;
    }

    return ssd1315_gram_fill_rect(&s_oled, left, top, right, bottom, on ? 1u : 0u) == 0u;
}

void oled_demo(void)
{
    if (!s_ready) {
        return;
    }

    (void)ssd1315_gram_fill_rect(&s_oled, 0u, 0u, OLED_WIDTH - 1u, OLED_HEIGHT - 1u, 0u);
    (void)ssd1315_gram_fill_rect(&s_oled, 0u, 0u, OLED_WIDTH - 1u, 0u, 1u);
    (void)ssd1315_gram_fill_rect(&s_oled, 0u, OLED_HEIGHT - 1u, OLED_WIDTH - 1u, OLED_HEIGHT - 1u, 1u);
    (void)ssd1315_gram_write_string(&s_oled, 2u, 8u, (char *)"SSD1315", 7u, 1u, SSD1315_FONT_16);
    (void)ssd1315_gram_write_string(&s_oled, 2u, 32u, (char *)"128x64 OK", 9u, 1u, SSD1315_FONT_16);
    (void)ssd1315_gram_update(&s_oled);
}
