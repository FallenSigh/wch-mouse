/**
 * @file paw3395.c
 * @brief PAW3395DM-T6QU optical mouse sensor driver for the CH585 (RISC-V).
 *
 * Register values and the 137-step power-up sequence are transcribed
 * verbatim from the PixArt PAW3395DM-T6QU datasheet (v0.8, 13 Jan 2021).
 *
 * SPI transport:
 *   - SPI0 master, mode 0, MSB first; SPI0_CLKCfg(8) -> 62.4/8 = 7.8 MHz
 *     (the vendor default divisor=4 would give 15.6 MHz, above the
 *     PAW3395's 10 MHz fSCLK limit, and the datasheet's tNCS-SCLK is
 *     measured to the first *rising* SCLK edge, i.e. SCLK idles low =
 *     mode 0)
 *   - CS is a manual GPIO output on PA12, exactly like the WCH SPI
 *     example.  PA13-15 are the SPI0 default mapping; do NOT enable
 *     RB_PIN_SPI0 (it remaps SPI0 to PB12-15 = USB D+ / SWDIO).
 *
 * Timing margins (datasheet minimums in parentheses):
 *   - tNCS-SCLK: 1 us (120 ns), tSRAD: 3 us (2 us),
 *   - tSWW/tSWR: 10 us (5 us),   tBEXIT:  2 us (500 ns)
 */

#include "paw3395.h"
#include "CH58x_common.h"
#include "CH58x_sys.h"
#include "log.h"

#include <stdio.h>

#define PIN_MS_CS     GPIO_Pin_12
#define PIN_MS_SCLK   GPIO_Pin_13
#define PIN_MS_MOSI   GPIO_Pin_14
#define PIN_MS_MISO   GPIO_Pin_15
#define PIN_MS_MOTION GPIO_Pin_17
#define PIN_MS_RST    GPIO_Pin_18

#define REG_PRODUCT_ID     0x00u
#define REG_REVISION_ID    0x01u
#define REG_MOTION         0x02u
#define REG_DELTA_X_L      0x03u
#define REG_DELTA_X_H      0x04u
#define REG_DELTA_Y_L      0x05u
#define REG_DELTA_Y_H      0x06u
#define REG_MOTION_BURST   0x16u
#define REG_POWER_UP_RESET 0x3Au
#define REG_SHUTDOWN       0x3Bu
#define REG_SET_RESOLUTION 0x47u
#define REG_RES_X_LOW      0x48u
#define REG_RES_X_HIGH     0x49u
#define REG_RES_Y_LOW      0x4Au
#define REG_RES_Y_HIGH     0x4Bu
#define REG_RIPPLE_CONTROL 0x5Au

#define CPI_MIN     50u
#define CPI_MAX     26000u
#define CPI_DEFAULT 5000u

static inline void cs_low(void) {
    GPIOA_ResetBits(PIN_MS_CS);
}
static inline void cs_high(void) {
    GPIOA_SetBits(PIN_MS_CS);
}

static inline void inter_xact_gap(void) {
    mDelayuS(10);
} /* >= tSWW/tSWR */
static inline void cs_setup_delay(void) {
    mDelayuS(1);
} /* >= tNCS-SCLK */
static inline void cs_hold_delay(void) {
    mDelayuS(2);
} /* >= tSCLK-NCS: 1us write, 120ns read */

static void paw3395_write_reg(uint8_t reg, uint8_t val) {
    cs_low();
    // cs_setup_delay();
    SPI0_MasterSendByte((uint8_t)(reg | 0x80u)); /* MSB=1 -> write  */
    SPI0_MasterSendByte(val);
    // cs_hold_delay();
    cs_high();
    // inter_xact_gap();
}

static uint8_t paw3395_read_reg(uint8_t reg) {
    cs_low();
    // cs_setup_delay();
    SPI0_MasterSendByte((uint8_t)(reg & 0x7Fu)); /* MSB=0 -> read   */
    // mDelayuS(3);                                 /* tSRAD >= 2 us   */
    uint8_t val = SPI0_MasterRecvByte();
    // cs_hold_delay();
    cs_high();
    // inter_xact_gap();
    return val;
}

static void paw3395_burst_read(uint8_t reg, uint8_t *buf, uint8_t len) {
    cs_low();
    // cs_setup_delay();
    SPI0_MasterSendByte((uint8_t)(reg & 0x7Fu));
    // mDelayuS(3); /* tSRAD           */
    SPI0_MasterRecv(buf, len);
    // cs_hold_delay();
    cs_high();
    // mDelayuS(2); /* tBEXIT >= 500ns */
}

bool paw3395_init(void) {
    /* GPIO: CS/SCLK/MOSI are outputs, MISO is an input. The SPI0
     * peripheral requires the pads to be in output mode before it can
     * drive them (see docs/CH585EVT/EVT/EXAM/SPI/src/Main.c, which sets
     * PA12|PA13|PA14 to GPIO_ModeOut_PP_5mA); R8_SPI0_CTRL_MOD's OE bits
     * alone are not enough. */
    GPIOA_SetBits(PIN_MS_SCLK | PIN_MS_MOSI | PIN_MS_CS);
    GPIOA_ModeCfg(PIN_MS_SCLK | PIN_MS_MOSI | PIN_MS_CS, GPIO_ModeOut_PP_5mA);
    GPIOA_ModeCfg(PIN_MS_MISO, GPIO_ModeIN_Floating);
    GPIOB_SetBits(PIN_MS_RST);
    GPIOB_ModeCfg(PIN_MS_RST, GPIO_ModeOut_PP_5mA);
    GPIOB_ModeCfg(PIN_MS_MOTION, GPIO_ModeIN_Floating);

    /* SPI0 master, mode 0, MSB first.  The vendor default (divisor=4)
     * would give 62.4 / 4 = 15.6 MHz, which exceeds the PAW3395's
     * 10 MHz fSCLK limit.  Override to divisor=8 -> 7.8 MHz. */
    SPI0_MasterDefInit();
    SPI0_DataMode(Mode0_HighBitINFront);
    SPI0_CLKCfg(8);

    GPIOB_ResetBits(PIN_MS_RST);
    DelayMs(10);
    GPIOB_SetBits(PIN_MS_RST);

    // wait for at least 50ms
    DelayMs(50);

    // drive NCS high, and then low to reset the SPI port
    cs_low();
    cs_setup_delay();
    cs_high();
    cs_hold_delay();
    cs_low();
    cs_setup_delay();

    // write 0x5a to Power_Up_Reset register
    paw3395_write_reg(REG_POWER_UP_RESET, 0x5A);
    
    // wait for at least 5ms
    DelayMs(5);

    // read registers from 0x02 to 0x06 one time
    for (uint8_t addr = 0x02u; addr <= 0x06u; addr++) {
        paw3395_read_reg(addr);
    }

    // power up initialization
    paw3395_write_reg(0x7F ,0x07);
	paw3395_write_reg(0x40 ,0x41);
	paw3395_write_reg(0x7F ,0x00);
	paw3395_write_reg(0x40 ,0x80);
	paw3395_write_reg(0x7F ,0x0E);
	paw3395_write_reg(0x55 ,0x0D);
	paw3395_write_reg(0x56 ,0x1B);
	paw3395_write_reg(0x57 ,0xE8);
	paw3395_write_reg(0x58 ,0xD5);
	paw3395_write_reg(0x7F ,0x14);
	paw3395_write_reg(0x42 ,0xBC);
	paw3395_write_reg(0x43 ,0x74);
	paw3395_write_reg(0x4B ,0x20);
	paw3395_write_reg(0x4D ,0x00);
	paw3395_write_reg(0x53 ,0x0E);
	paw3395_write_reg(0x7F ,0x05);
	paw3395_write_reg(0x44 ,0x04);
	paw3395_write_reg(0x4D ,0x06);
	paw3395_write_reg(0x51 ,0x40);
	paw3395_write_reg(0x53 ,0x40);
	paw3395_write_reg(0x55 ,0xCA);
	paw3395_write_reg(0x5A ,0xE8);
	paw3395_write_reg(0x5B ,0xEA);
	paw3395_write_reg(0x61 ,0x31);
	paw3395_write_reg(0x62 ,0x64);
	paw3395_write_reg(0x6D ,0xB8);
	paw3395_write_reg(0x6E ,0x0F);

	paw3395_write_reg(0x70 ,0x02);
	paw3395_write_reg(0x4A ,0x2A);
	paw3395_write_reg(0x60 ,0x26);
	paw3395_write_reg(0x7F ,0x06);
	paw3395_write_reg(0x6D ,0x70);
	paw3395_write_reg(0x6E ,0x60);
	paw3395_write_reg(0x6F ,0x04);
	paw3395_write_reg(0x53 ,0x02);
	paw3395_write_reg(0x55 ,0x11);
	paw3395_write_reg(0x7A ,0x01);
	paw3395_write_reg(0x7D ,0x51);
	paw3395_write_reg(0x7F ,0x07);
	paw3395_write_reg(0x41 ,0x10);
	paw3395_write_reg(0x42 ,0x32);
	paw3395_write_reg(0x43 ,0x00);
	paw3395_write_reg(0x7F ,0x08);
	paw3395_write_reg(0x71 ,0x4F);
	paw3395_write_reg(0x7F ,0x09);
	paw3395_write_reg(0x62 ,0x1F);
	paw3395_write_reg(0x63 ,0x1F);
	paw3395_write_reg(0x65 ,0x03);
	paw3395_write_reg(0x66 ,0x03);
	paw3395_write_reg(0x67 ,0x1F);
	paw3395_write_reg(0x68 ,0x1F);
	paw3395_write_reg(0x69 ,0x03);
	paw3395_write_reg(0x6A ,0x03);
	paw3395_write_reg(0x6C ,0x1F);

	paw3395_write_reg(0x6D ,0x1F);
	paw3395_write_reg(0x51 ,0x04);
	paw3395_write_reg(0x53 ,0x20);
	paw3395_write_reg(0x54 ,0x20);
	paw3395_write_reg(0x71 ,0x0C);
	paw3395_write_reg(0x72 ,0x07);
	paw3395_write_reg(0x73 ,0x07);
	paw3395_write_reg(0x7F ,0x0A);
	paw3395_write_reg(0x4A ,0x14);
	paw3395_write_reg(0x4C ,0x14);
	paw3395_write_reg(0x55 ,0x19);
	paw3395_write_reg(0x7F ,0x14);
	paw3395_write_reg(0x4B ,0x30);
	paw3395_write_reg(0x4C ,0x03);
	paw3395_write_reg(0x61 ,0x0B);
	paw3395_write_reg(0x62 ,0x0A);
	paw3395_write_reg(0x63 ,0x02);
	paw3395_write_reg(0x7F ,0x15);
	paw3395_write_reg(0x4C ,0x02);
	paw3395_write_reg(0x56 ,0x02);
	paw3395_write_reg(0x41 ,0x91);
	paw3395_write_reg(0x4D ,0x0A);
	paw3395_write_reg(0x7F ,0x0C);
	paw3395_write_reg(0x4A ,0x10);
	paw3395_write_reg(0x4B ,0x0C);
	paw3395_write_reg(0x4C ,0x40);
	paw3395_write_reg(0x41 ,0x25);
	paw3395_write_reg(0x55 ,0x18);
	paw3395_write_reg(0x56 ,0x14);
	paw3395_write_reg(0x49 ,0x0A);
	paw3395_write_reg(0x42 ,0x00);
	paw3395_write_reg(0x43 ,0x2D);
	paw3395_write_reg(0x44 ,0x0C);
	paw3395_write_reg(0x54 ,0x1A);
	paw3395_write_reg(0x5A ,0x0D);
	paw3395_write_reg(0x5F ,0x1E);
	paw3395_write_reg(0x5B ,0x05);
	paw3395_write_reg(0x5E ,0x0F);
	paw3395_write_reg(0x7F ,0x0D);
	paw3395_write_reg(0x48 ,0xDD);
	paw3395_write_reg(0x4F ,0x03);
	paw3395_write_reg(0x52 ,0x49);
		
	paw3395_write_reg(0x51 ,0x00);
	paw3395_write_reg(0x54 ,0x5B);
	paw3395_write_reg(0x53 ,0x00);
		
	paw3395_write_reg(0x56 ,0x64);
	paw3395_write_reg(0x55 ,0x00);
	paw3395_write_reg(0x58 ,0xA5);
	paw3395_write_reg(0x57 ,0x02);
	paw3395_write_reg(0x5A ,0x29);
	paw3395_write_reg(0x5B ,0x47);
	paw3395_write_reg(0x5C ,0x81);
	paw3395_write_reg(0x5D ,0x40);
	paw3395_write_reg(0x71 ,0xDC);
	paw3395_write_reg(0x70 ,0x07);
	paw3395_write_reg(0x73 ,0x00);
	paw3395_write_reg(0x72 ,0x08);
	paw3395_write_reg(0x75 ,0xDC);
	paw3395_write_reg(0x74 ,0x07);
	paw3395_write_reg(0x77 ,0x00);
	paw3395_write_reg(0x76 ,0x08);
	paw3395_write_reg(0x7F ,0x10);
	paw3395_write_reg(0x4C ,0xD0);
	paw3395_write_reg(0x7F ,0x00);
	paw3395_write_reg(0x4F ,0x63);
	paw3395_write_reg(0x4E ,0x00);
	paw3395_write_reg(0x52 ,0x63);
	paw3395_write_reg(0x51 ,0x00);
	paw3395_write_reg(0x54 ,0x54);
	paw3395_write_reg(0x5A ,0x10);
	paw3395_write_reg(0x77 ,0x4F);
	paw3395_write_reg(0x47 ,0x01);
	paw3395_write_reg(0x5B ,0x40);
	paw3395_write_reg(0x64 ,0x60);
	paw3395_write_reg(0x65 ,0x06);
	paw3395_write_reg(0x66 ,0x13);
	paw3395_write_reg(0x67 ,0x0F);
	paw3395_write_reg(0x78 ,0x01);
	paw3395_write_reg(0x79 ,0x9C);
	paw3395_write_reg(0x40 ,0x00);
	paw3395_write_reg(0x55 ,0x02);
	paw3395_write_reg(0x23 ,0x70);
	paw3395_write_reg(0x22 ,0x01);

    // wait for 1ms
    DelayMs(1);

    // read register 0x6C as 1ms interval until value 
    // 0x80 is obtained for read up to 60 times
    // this register read interval must be carried out at 1ms
    // interval with timing tolerance of 1%
    int i, val;
    for (i = 0; i < 60; i++) {
        val = paw3395_read_reg(0x6C);
        if (val == 0x80) {
            break;
        }
        DelayMs(1);
    }

    // if value 0x80 is not obtained from
    // register 0x6C after 60 times
    if (i == 60) {
        paw3395_write_reg(0x7F, 0x14);
        paw3395_write_reg(0x6C, 0x00);
        paw3395_write_reg(0x7F, 0x00);
    }

    paw3395_write_reg(0x22, 0x00);
    paw3395_write_reg(0x55, 0x00);
    paw3395_write_reg(0x7F, 0x07);
    paw3395_write_reg(0x40, 0x40);
    paw3395_write_reg(0x7F, 0x00);

    LOG_I("PAW3395", "register 0x6C reaches %#x at %d", val, i);

    return true;
}

void paw3395_set_cpi(uint16_t cpi) {
	/* Clamp to the datasheet's supported range. */
    if (cpi < CPI_MIN) { cpi = CPI_MIN; }
    if (cpi > CPI_MAX) { cpi = CPI_MAX; }

    /* Datasheet: cpi = 50 * (reg + 1)  ->  reg = cpi / 50 - 1 */
    uint16_t reg = (uint16_t)((cpi / 50u) - 1u);

    /* Resolution_X / Resolution_Y: write low byte first, then high. */
    paw3395_write_reg(REG_RES_X_LOW,  (uint8_t)(reg & 0xFFu));
    paw3395_write_reg(REG_RES_X_HIGH, (uint8_t)(reg >> 8));
    paw3395_write_reg(REG_RES_Y_LOW,  (uint8_t)(reg & 0xFFu));
    paw3395_write_reg(REG_RES_Y_HIGH, (uint8_t)(reg >> 8));

    /* Commit via Set_Resolution. */
    paw3395_write_reg(REG_SET_RESOLUTION, 0x01u);

    /* nable RIPPLE_CONTROL bit-7 for CPI >= 9000. */
    if (cpi >= 9000u) {
        uint8_t ripple = paw3395_read_reg(REG_RIPPLE_CONTROL);
        paw3395_write_reg(REG_RIPPLE_CONTROL, (uint8_t)(ripple | 0x80u));
    }
}

void paw3395_burst(uint8_t* buf) {
	paw3395_burst_read(0x16, buf, 12);
}

void paw3395_shutdown() {
	paw3395_write_reg(REG_SHUTDOWN, 0xB6);
}