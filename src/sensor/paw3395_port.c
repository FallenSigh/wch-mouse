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
/* Flying-wire copy of MOTION on PA7 (PB17 works for polling but cannot raise a
 * GPIO interrupt; PA7 can). */
#define PIN_MS_MOTION_ALT GPIO_Pin_7

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

bool paw3395_motion_ready(void) {
    return GPIOA_ReadPortPin(PIN_MS_MOTION_ALT) == 0u;
}

/* --- IRQ-driven motion burst -------------------------------------------- */

static volatile bool    s_motion_on;
static volatile bool    s_motion_parked;
static volatile int32_t s_motion_dx;
static volatile int32_t s_motion_dy;
static __attribute__((aligned(4))) uint8_t s_motion_buf[PAW3395_MOTION_BURST_LEN];

/* One burst read into the accumulator, through the vendor's DMA helper. Callers
 * must exclude each other: the scan-loop call masks the GPIO interrupt, and the
 * ISR is the only other user. */
static void paw3395_motion_burst(void) {
    int16_t dx;
    int16_t dy;

    cs_low();
    SPI0_MasterSendByte(PAW3395_MOTION_BURST_REG);
    SPI0_MasterDMARecv(s_motion_buf, PAW3395_MOTION_BURST_LEN);
    cs_high();

    paw3395_parse_burst(s_motion_buf, &dx, &dy);
    s_motion_dx += dx;
    s_motion_dy += dy;
}

/* Runs from RAM: in standby this fires as the wake-up event, at a point where
 * the flash and the 32M clock are still down, so nothing here may execute from
 * flash. */
__INTERRUPT __HIGH_CODE void GPIOA_IRQHandler(void) {
    if (R16_PA_INT_IF & PIN_MS_MOTION_ALT) {
        R16_PA_INT_IF = PIN_MS_MOTION_ALT;   /* RW1: clear */

        /* Parked: record only. The burst needs the SPI bus and the flash, and
         * neither is up yet - paw3395_motion_start() picks the sample up. */
        if (s_motion_on && !s_motion_parked) {
            paw3395_motion_burst();
        }
    }
}

void paw3395_motion_park(void) {
    s_motion_parked = true;
}

void paw3395_motion_start(void) {
    s_motion_parked = false;
    s_motion_dx = 0;
    s_motion_dy = 0;
    s_motion_on = false;

    R16_PA_INT_IF = PIN_MS_MOTION_ALT;
    GPIOA_ITModeCfg(PIN_MS_MOTION_ALT, GPIO_ITMode_FallEdge);

    s_motion_on = true;

    /* Catch a sample that was already waiting before the interrupt went live. */
    if (paw3395_motion_ready()) {
        paw3395_motion_burst();
    }

    PFIC_EnableIRQ(GPIO_A_IRQn);
}

void paw3395_motion_stop(void) {
    s_motion_on = false;
    PFIC_DisableIRQ(GPIO_A_IRQn);
    R16_PA_INT_EN &= ~PIN_MS_MOTION_ALT;
    R16_PA_INT_IF = PIN_MS_MOTION_ALT;
}

void paw3395_motion_read(int16_t *dx, int16_t *dy) {
    int32_t x;
    int32_t y;

    /* Top up if the pin is still asserted; mask the interrupt so a falling edge
     * cannot start a nested burst on the SPI bus. */
    PFIC_DisableIRQ(GPIO_A_IRQn);
    if (s_motion_on && paw3395_motion_ready()) {
        paw3395_motion_burst();
    }
    PFIC_EnableIRQ(GPIO_A_IRQn);

    PFIC_DisableAllIRQ();
    x = s_motion_dx;
    y = s_motion_dy;
    s_motion_dx = 0;
    s_motion_dy = 0;
    PFIC_EnableAllIRQ();

    if (x > 32767) {
        x = 32767;
    } else if (x < -32768) {
        x = -32768;
    }
    if (y > 32767) {
        y = 32767;
    } else if (y < -32768) {
        y = -32768;
    }

    *dx = (int16_t)x;
    *dy = (int16_t)y;
}

bool paw3395_port_init(struct paw3395_dev *dev) {
    GPIOA_SetBits(PIN_MS_SCLK | PIN_MS_MOSI | PIN_MS_CS);
    GPIOA_ModeCfg(PIN_MS_SCLK | PIN_MS_MOSI | PIN_MS_CS, GPIO_ModeOut_PP_5mA);
    GPIOA_ModeCfg(PIN_MS_MISO, GPIO_ModeIN_Floating);
    GPIOB_SetBits(PIN_MS_RST);
    GPIOB_ModeCfg(PIN_MS_RST, GPIO_ModeOut_PP_5mA);
    GPIOB_ModeCfg(PIN_MS_MOTION, GPIO_ModeIN_Floating);
    GPIOA_ModeCfg(PIN_MS_MOTION_ALT, GPIO_ModeIN_PU);

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
