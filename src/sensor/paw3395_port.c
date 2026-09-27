/**
 * @file paw3395_port.c
 * @brief SPI0/GPIO binding for the PAW3395 sensor (WCH CH585).
 *
 * SPI0 master, mode 0, MSB first; SPI0_CLKCfg(8) -> 62.4/8 = 7.8 MHz (the
 * vendor default divisor=4 would give 15.6 MHz, above the PAW3395's 10 MHz
 * fSCLK limit, and the datasheet's tNCS-SCLK is measured to the first *rising*
 * SCLK edge, i.e. SCLK idles low = mode 0). CS is a manual GPIO output on
 * PA12, exactly like the WCH SPI example. PA13-15 are the SPI0 default
 * mapping; do NOT enable RB_PIN_SPI0 (it remaps SPI0 to PB12-15 = USB D+ /
 * SWDIO).
 */

#include "paw3395_port.h"

#include "CH58x_common.h"
#include "CH58x_sys.h"

#define PIN_MS_CS     GPIO_Pin_12
#define PIN_MS_SCLK   GPIO_Pin_13
#define PIN_MS_MOSI   GPIO_Pin_14
#define PIN_MS_MISO   GPIO_Pin_15
#define PIN_MS_MOTION GPIO_Pin_17
#define PIN_MS_RST    GPIO_Pin_18

static inline void cs_low(void) {
    GPIOA_ResetBits(PIN_MS_CS);
}

static inline void cs_high(void) {
    GPIOA_SetBits(PIN_MS_CS);
}

/* PAW3395 uses MSB=0 for read, MSB=1 for write (the opposite of the BMI270). */
static uint8_t paw3395_spi_read(uint8_t reg, uint8_t *data, uint16_t len) {
    cs_low();
    SPI0_MasterSendByte((uint8_t)(reg & 0x7Fu));
    SPI0_MasterRecv(data, len);
    cs_high();
    return 0u;
}

static void paw3395_spi_write(uint8_t reg, const uint8_t *data, uint16_t len) {
    cs_low();
    SPI0_MasterSendByte((uint8_t)(reg | 0x80u));
    SPI0_MasterTrans((uint8_t *)data, len);
    cs_high();
}

static void paw3395_delay_us(uint32_t period) {
    DelayUs(period);
}

bool paw3395_port_init(struct paw3395_dev *dev) {
    GPIOA_SetBits(PIN_MS_SCLK | PIN_MS_MOSI | PIN_MS_CS);
    GPIOA_ModeCfg(PIN_MS_SCLK | PIN_MS_MOSI | PIN_MS_CS, GPIO_ModeOut_PP_5mA);
    GPIOA_ModeCfg(PIN_MS_MISO, GPIO_ModeIN_Floating);
    GPIOB_SetBits(PIN_MS_RST);
    GPIOB_ModeCfg(PIN_MS_RST, GPIO_ModeOut_PP_5mA);
    GPIOB_ModeCfg(PIN_MS_MOTION, GPIO_ModeIN_Floating);

    SPI0_MasterDefInit();
    SPI0_DataMode(Mode0_HighBitINFront);
    SPI0_CLKCfg(8);

    dev->read = paw3395_spi_read;
    dev->write = paw3395_spi_write;
    dev->delay_us = paw3395_delay_us;

    GPIOB_ResetBits(PIN_MS_RST);
    mDelaymS(10);
    GPIOB_SetBits(PIN_MS_RST);

    /* Module boot time. */
    mDelaymS(50);

    /* Drive NCS high then low to reset the sensor's SPI port. */
    cs_low();
    mDelayuS(1);
    cs_high();
    mDelayuS(2);
    cs_low();
    mDelayuS(1);

    return paw3395_init(dev);
}
