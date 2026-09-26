/**
 * @file paw3395.c
 * @brief PAW3395DM-T6QU optical mouse sensor driver (transport-agnostic).
 *
 * Register values and the 137-step power-up sequence are transcribed
 * verbatim from the PixArt PAW3395DM-T6QU datasheet (v0.8, 13 Jan 2021).
 *
 * The SPI bus and chip-select timing are owned by the port layer
 * (paw3395_port.c); this file reaches the sensor only through the
 * `struct paw3395_dev` transport callbacks.
 */

#include "paw3395.h"
#include "log.h"

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
#define BURST_LEN   12u

static void paw3395_write_reg(struct paw3395_dev *dev, uint8_t reg, uint8_t val) {
    dev->write(reg, &val, 1u);
}

static uint8_t paw3395_read_reg(struct paw3395_dev *dev, uint8_t reg) {
    uint8_t val = 0;

    dev->read(reg, &val, 1u);
    return val;
}

static void paw3395_delay_ms(struct paw3395_dev *dev, uint32_t ms) {
    dev->delay_us(ms * 1000u);
}

bool paw3395_init(struct paw3395_dev *dev) {
    // write 0x5a to Power_Up_Reset register
    paw3395_write_reg(dev, REG_POWER_UP_RESET, 0x5A);
    
    // wait for at least 5ms
    paw3395_delay_ms(dev, 5);

    // read registers from 0x02 to 0x06 one time
    for (uint8_t addr = 0x02u; addr <= 0x06u; addr++) {
        paw3395_read_reg(dev, addr);
    }

    // power up initialization
    paw3395_write_reg(dev, 0x7F ,0x07);
	paw3395_write_reg(dev, 0x40 ,0x41);
	paw3395_write_reg(dev, 0x7F ,0x00);
	paw3395_write_reg(dev, 0x40 ,0x80);
	paw3395_write_reg(dev, 0x7F ,0x0E);
	paw3395_write_reg(dev, 0x55 ,0x0D);
	paw3395_write_reg(dev, 0x56 ,0x1B);
	paw3395_write_reg(dev, 0x57 ,0xE8);
	paw3395_write_reg(dev, 0x58 ,0xD5);
	paw3395_write_reg(dev, 0x7F ,0x14);
	paw3395_write_reg(dev, 0x42 ,0xBC);
	paw3395_write_reg(dev, 0x43 ,0x74);
	paw3395_write_reg(dev, 0x4B ,0x20);
	paw3395_write_reg(dev, 0x4D ,0x00);
	paw3395_write_reg(dev, 0x53 ,0x0E);
	paw3395_write_reg(dev, 0x7F ,0x05);
	paw3395_write_reg(dev, 0x44 ,0x04);
	paw3395_write_reg(dev, 0x4D ,0x06);
	paw3395_write_reg(dev, 0x51 ,0x40);
	paw3395_write_reg(dev, 0x53 ,0x40);
	paw3395_write_reg(dev, 0x55 ,0xCA);
	paw3395_write_reg(dev, 0x5A ,0xE8);
	paw3395_write_reg(dev, 0x5B ,0xEA);
	paw3395_write_reg(dev, 0x61 ,0x31);
	paw3395_write_reg(dev, 0x62 ,0x64);
	paw3395_write_reg(dev, 0x6D ,0xB8);
	paw3395_write_reg(dev, 0x6E ,0x0F);

	paw3395_write_reg(dev, 0x70 ,0x02);
	paw3395_write_reg(dev, 0x4A ,0x2A);
	paw3395_write_reg(dev, 0x60 ,0x26);
	paw3395_write_reg(dev, 0x7F ,0x06);
	paw3395_write_reg(dev, 0x6D ,0x70);
	paw3395_write_reg(dev, 0x6E ,0x60);
	paw3395_write_reg(dev, 0x6F ,0x04);
	paw3395_write_reg(dev, 0x53 ,0x02);
	paw3395_write_reg(dev, 0x55 ,0x11);
	paw3395_write_reg(dev, 0x7A ,0x01);
	paw3395_write_reg(dev, 0x7D ,0x51);
	paw3395_write_reg(dev, 0x7F ,0x07);
	paw3395_write_reg(dev, 0x41 ,0x10);
	paw3395_write_reg(dev, 0x42 ,0x32);
	paw3395_write_reg(dev, 0x43 ,0x00);
	paw3395_write_reg(dev, 0x7F ,0x08);
	paw3395_write_reg(dev, 0x71 ,0x4F);
	paw3395_write_reg(dev, 0x7F ,0x09);
	paw3395_write_reg(dev, 0x62 ,0x1F);
	paw3395_write_reg(dev, 0x63 ,0x1F);
	paw3395_write_reg(dev, 0x65 ,0x03);
	paw3395_write_reg(dev, 0x66 ,0x03);
	paw3395_write_reg(dev, 0x67 ,0x1F);
	paw3395_write_reg(dev, 0x68 ,0x1F);
	paw3395_write_reg(dev, 0x69 ,0x03);
	paw3395_write_reg(dev, 0x6A ,0x03);
	paw3395_write_reg(dev, 0x6C ,0x1F);

	paw3395_write_reg(dev, 0x6D ,0x1F);
	paw3395_write_reg(dev, 0x51 ,0x04);
	paw3395_write_reg(dev, 0x53 ,0x20);
	paw3395_write_reg(dev, 0x54 ,0x20);
	paw3395_write_reg(dev, 0x71 ,0x0C);
	paw3395_write_reg(dev, 0x72 ,0x07);
	paw3395_write_reg(dev, 0x73 ,0x07);
	paw3395_write_reg(dev, 0x7F ,0x0A);
	paw3395_write_reg(dev, 0x4A ,0x14);
	paw3395_write_reg(dev, 0x4C ,0x14);
	paw3395_write_reg(dev, 0x55 ,0x19);
	paw3395_write_reg(dev, 0x7F ,0x14);
	paw3395_write_reg(dev, 0x4B ,0x30);
	paw3395_write_reg(dev, 0x4C ,0x03);
	paw3395_write_reg(dev, 0x61 ,0x0B);
	paw3395_write_reg(dev, 0x62 ,0x0A);
	paw3395_write_reg(dev, 0x63 ,0x02);
	paw3395_write_reg(dev, 0x7F ,0x15);
	paw3395_write_reg(dev, 0x4C ,0x02);
	paw3395_write_reg(dev, 0x56 ,0x02);
	paw3395_write_reg(dev, 0x41 ,0x91);
	paw3395_write_reg(dev, 0x4D ,0x0A);
	paw3395_write_reg(dev, 0x7F ,0x0C);
	paw3395_write_reg(dev, 0x4A ,0x10);
	paw3395_write_reg(dev, 0x4B ,0x0C);
	paw3395_write_reg(dev, 0x4C ,0x40);
	paw3395_write_reg(dev, 0x41 ,0x25);
	paw3395_write_reg(dev, 0x55 ,0x18);
	paw3395_write_reg(dev, 0x56 ,0x14);
	paw3395_write_reg(dev, 0x49 ,0x0A);
	paw3395_write_reg(dev, 0x42 ,0x00);
	paw3395_write_reg(dev, 0x43 ,0x2D);
	paw3395_write_reg(dev, 0x44 ,0x0C);
	paw3395_write_reg(dev, 0x54 ,0x1A);
	paw3395_write_reg(dev, 0x5A ,0x0D);
	paw3395_write_reg(dev, 0x5F ,0x1E);
	paw3395_write_reg(dev, 0x5B ,0x05);
	paw3395_write_reg(dev, 0x5E ,0x0F);
	paw3395_write_reg(dev, 0x7F ,0x0D);
	paw3395_write_reg(dev, 0x48 ,0xDD);
	paw3395_write_reg(dev, 0x4F ,0x03);
	paw3395_write_reg(dev, 0x52 ,0x49);
		
	paw3395_write_reg(dev, 0x51 ,0x00);
	paw3395_write_reg(dev, 0x54 ,0x5B);
	paw3395_write_reg(dev, 0x53 ,0x00);
		
	paw3395_write_reg(dev, 0x56 ,0x64);
	paw3395_write_reg(dev, 0x55 ,0x00);
	paw3395_write_reg(dev, 0x58 ,0xA5);
	paw3395_write_reg(dev, 0x57 ,0x02);
	paw3395_write_reg(dev, 0x5A ,0x29);
	paw3395_write_reg(dev, 0x5B ,0x47);
	paw3395_write_reg(dev, 0x5C ,0x81);
	paw3395_write_reg(dev, 0x5D ,0x40);
	paw3395_write_reg(dev, 0x71 ,0xDC);
	paw3395_write_reg(dev, 0x70 ,0x07);
	paw3395_write_reg(dev, 0x73 ,0x00);
	paw3395_write_reg(dev, 0x72 ,0x08);
	paw3395_write_reg(dev, 0x75 ,0xDC);
	paw3395_write_reg(dev, 0x74 ,0x07);
	paw3395_write_reg(dev, 0x77 ,0x00);
	paw3395_write_reg(dev, 0x76 ,0x08);
	paw3395_write_reg(dev, 0x7F ,0x10);
	paw3395_write_reg(dev, 0x4C ,0xD0);
	paw3395_write_reg(dev, 0x7F ,0x00);
	paw3395_write_reg(dev, 0x4F ,0x63);
	paw3395_write_reg(dev, 0x4E ,0x00);
	paw3395_write_reg(dev, 0x52 ,0x63);
	paw3395_write_reg(dev, 0x51 ,0x00);
	paw3395_write_reg(dev, 0x54 ,0x54);
	paw3395_write_reg(dev, 0x5A ,0x10);
	paw3395_write_reg(dev, 0x77 ,0x4F);
	paw3395_write_reg(dev, 0x47 ,0x01);
	paw3395_write_reg(dev, 0x5B ,0x40);
	paw3395_write_reg(dev, 0x64 ,0x60);
	paw3395_write_reg(dev, 0x65 ,0x06);
	paw3395_write_reg(dev, 0x66 ,0x13);
	paw3395_write_reg(dev, 0x67 ,0x0F);
	paw3395_write_reg(dev, 0x78 ,0x01);
	paw3395_write_reg(dev, 0x79 ,0x9C);
	paw3395_write_reg(dev, 0x40 ,0x00);
	paw3395_write_reg(dev, 0x55 ,0x02);
	paw3395_write_reg(dev, 0x23 ,0x70);
	paw3395_write_reg(dev, 0x22 ,0x01);

    // wait for 1ms
    paw3395_delay_ms(dev, 1);

    // read register 0x6C as 1ms interval until value 
    // 0x80 is obtained for read up to 60 times
    // this register read interval must be carried out at 1ms
    // interval with timing tolerance of 1%
    int i, val;
    for (i = 0; i < 60; i++) {
        val = paw3395_read_reg(dev, 0x6C);
        if (val == 0x80) {
            break;
        }
        paw3395_delay_ms(dev, 1);
    }

    // if value 0x80 is not obtained from
    // register 0x6C after 60 times
    if (i == 60) {
        paw3395_write_reg(dev, 0x7F, 0x14);
        paw3395_write_reg(dev, 0x6C, 0x00);
        paw3395_write_reg(dev, 0x7F, 0x00);
    }

    paw3395_write_reg(dev, 0x22, 0x00);
    paw3395_write_reg(dev, 0x55, 0x00);
    paw3395_write_reg(dev, 0x7F, 0x07);
    paw3395_write_reg(dev, 0x40, 0x40);
    paw3395_write_reg(dev, 0x7F, 0x00);

    LOG_I("PAW3395", "register 0x6C reaches %#x at %d", val, i);

    dev->initialized = true;
    dev->mode = PAW3395_MODE_HIGH_PERFORMANCE;
    dev->lift_cut = PAW3395_LIFT_CUT_1MM;
    return true;
}

void paw3395_set_cpi(struct paw3395_dev *dev, uint16_t cpi) {
	/* Clamp to the datasheet's supported range. */
    if (cpi < CPI_MIN) { cpi = CPI_MIN; }
    if (cpi > CPI_MAX) { cpi = CPI_MAX; }

    /* Datasheet: cpi = 50 * (reg + 1)  ->  reg = cpi / 50 - 1 */
    uint16_t reg = (uint16_t)((cpi / 50u) - 1u);

    /* Resolution_X / Resolution_Y: write low byte first, then high. */
    paw3395_write_reg(dev, REG_RES_X_LOW,  (uint8_t)(reg & 0xFFu));
    paw3395_write_reg(dev, REG_RES_X_HIGH, (uint8_t)(reg >> 8));
    paw3395_write_reg(dev, REG_RES_Y_LOW,  (uint8_t)(reg & 0xFFu));
    paw3395_write_reg(dev, REG_RES_Y_HIGH, (uint8_t)(reg >> 8));

    /* Commit via Set_Resolution. */
    paw3395_write_reg(dev, REG_SET_RESOLUTION, 0x01u);

    /* nable RIPPLE_CONTROL bit-7 for CPI >= 9000. */
    if (cpi >= 9000u) {
        uint8_t ripple = paw3395_read_reg(dev, REG_RIPPLE_CONTROL);
        paw3395_write_reg(dev, REG_RIPPLE_CONTROL, (uint8_t)(ripple | 0x80u));
    }

    dev->cpi = cpi;
}

/* Datasheet power/performance mode tables, one per mode. `mask` is 0 for a
 * plain write; otherwise only those bits are updated (read-modify-write). */
struct paw3395_regval {
    uint8_t reg;
    uint8_t val;
    uint8_t mask;
};

static const struct paw3395_regval paw3395_mode_high_performance[] = {
    {0x7Fu, 0x05u, 0x00u}, {0x51u, 0x40u, 0x00u}, {0x53u, 0x40u, 0x00u},
    {0x61u, 0x31u, 0x00u}, {0x6Eu, 0x0Fu, 0x00u}, {0x7Fu, 0x07u, 0x00u},
    {0x42u, 0x32u, 0x00u}, {0x43u, 0x00u, 0x00u}, {0x7Fu, 0x0Du, 0x00u},
    {0x51u, 0x00u, 0x00u}, {0x52u, 0x49u, 0x00u}, {0x53u, 0x00u, 0x00u},
    {0x54u, 0x5Bu, 0x00u}, {0x55u, 0x00u, 0x00u}, {0x56u, 0x64u, 0x00u},
    {0x57u, 0x02u, 0x00u}, {0x58u, 0xA5u, 0x00u}, {0x7Fu, 0x00u, 0x00u},
    {0x54u, 0x54u, 0x00u}, {0x78u, 0x01u, 0x00u}, {0x79u, 0x9Cu, 0x00u},
    {0x40u, 0x00u, 0x03u},
};

static const struct paw3395_regval paw3395_mode_low_power[] = {
    {0x7Fu, 0x05u, 0x00u}, {0x51u, 0x40u, 0x00u}, {0x53u, 0x40u, 0x00u},
    {0x61u, 0x3Bu, 0x00u}, {0x6Eu, 0x1Fu, 0x00u}, {0x7Fu, 0x07u, 0x00u},
    {0x42u, 0x32u, 0x00u}, {0x43u, 0x00u, 0x00u}, {0x7Fu, 0x0Du, 0x00u},
    {0x51u, 0x00u, 0x00u}, {0x52u, 0x49u, 0x00u}, {0x53u, 0x00u, 0x00u},
    {0x54u, 0x5Bu, 0x00u}, {0x55u, 0x00u, 0x00u}, {0x56u, 0x64u, 0x00u},
    {0x57u, 0x02u, 0x00u}, {0x58u, 0xA5u, 0x00u}, {0x7Fu, 0x00u, 0x00u},
    {0x54u, 0x54u, 0x00u}, {0x78u, 0x01u, 0x00u}, {0x79u, 0x9Cu, 0x00u},
    {0x40u, 0x01u, 0x03u},
};

static const struct paw3395_regval paw3395_mode_office[] = {
    {0x7Fu, 0x05u, 0x00u}, {0x51u, 0x28u, 0x00u}, {0x53u, 0x30u, 0x00u},
    {0x61u, 0x3Bu, 0x00u}, {0x6Eu, 0x1Fu, 0x00u}, {0x7Fu, 0x07u, 0x00u},
    {0x42u, 0x32u, 0x00u}, {0x43u, 0x00u, 0x00u}, {0x7Fu, 0x0Du, 0x00u},
    {0x51u, 0x00u, 0x00u}, {0x52u, 0x49u, 0x00u}, {0x53u, 0x00u, 0x00u},
    {0x54u, 0x5Bu, 0x00u}, {0x55u, 0x00u, 0x00u}, {0x56u, 0x64u, 0x00u},
    {0x57u, 0x02u, 0x00u}, {0x58u, 0xA5u, 0x00u}, {0x7Fu, 0x00u, 0x00u},
    {0x54u, 0x52u, 0x00u}, {0x78u, 0x0Au, 0x00u}, {0x79u, 0x0Fu, 0x00u},
    {0x40u, 0x02u, 0x03u},
};

static const struct paw3395_regval paw3395_mode_corded_gaming[] = {
    {0x7Fu, 0x05u, 0x00u}, {0x51u, 0x40u, 0x00u}, {0x53u, 0x40u, 0x00u},
    {0x61u, 0x31u, 0x00u}, {0x6Eu, 0x0Fu, 0x00u}, {0x7Fu, 0x07u, 0x00u},
    {0x42u, 0x2Fu, 0x00u}, {0x43u, 0x00u, 0x00u}, {0x7Fu, 0x0Du, 0x00u},
    {0x51u, 0x12u, 0x00u}, {0x52u, 0xDBu, 0x00u}, {0x53u, 0x12u, 0x00u},
    {0x54u, 0xDCu, 0x00u}, {0x55u, 0x12u, 0x00u}, {0x56u, 0xEAu, 0x00u},
    {0x57u, 0x15u, 0x00u}, {0x58u, 0x2Du, 0x00u}, {0x7Fu, 0x00u, 0x00u},
    {0x54u, 0x55u, 0x00u}, {0x40u, 0x83u, 0x00u},
};

#define PAW3395_ROWS(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))

struct paw3395_mode_desc {
    const struct paw3395_regval *rows;
    uint8_t                      count;
};

static const struct paw3395_mode_desc paw3395_modes[PAW3395_MODE_COUNT] = {
    {paw3395_mode_high_performance, PAW3395_ROWS(paw3395_mode_high_performance)},
    {paw3395_mode_low_power,        PAW3395_ROWS(paw3395_mode_low_power)},
    {paw3395_mode_office,           PAW3395_ROWS(paw3395_mode_office)},
    {paw3395_mode_corded_gaming,    PAW3395_ROWS(paw3395_mode_corded_gaming)},
};

void paw3395_set_mode(struct paw3395_dev *dev, enum paw3395_mode mode) {
    if ((unsigned)mode >= (unsigned)PAW3395_MODE_COUNT) {
        mode = PAW3395_MODE_HIGH_PERFORMANCE;
    }

    const struct paw3395_mode_desc *desc = &paw3395_modes[mode];

    for (uint8_t i = 0; i < desc->count; i++) {
        const struct paw3395_regval *row = &desc->rows[i];

        if (row->mask == 0u) {
            paw3395_write_reg(dev, row->reg, row->val);
        } else {
            const uint8_t cur = paw3395_read_reg(dev, row->reg);
            paw3395_write_reg(dev, row->reg, (uint8_t)((cur & ~row->mask) | (row->val & row->mask)));
        }
    }

    dev->mode = mode;
}

void paw3395_set_lift_cut(struct paw3395_dev *dev, enum paw3395_lift_cut cut) {
    /* Universal lift cut-off: page 0x0C, register 0x4E. Bit 1 selects the
     * 2 mm setting; 0 (the power-up default) is 1 mm. */
    const uint8_t bits = (cut == PAW3395_LIFT_CUT_2MM) ? 0x02u : 0x00u;

    paw3395_write_reg(dev, 0x7Fu, 0x0Cu);
    const uint8_t cur = paw3395_read_reg(dev, 0x4Eu);
    paw3395_write_reg(dev, 0x4Eu, (uint8_t)((cur & (uint8_t)~0x02u) | bits));
    paw3395_write_reg(dev, 0x7Fu, 0x00u);

    dev->lift_cut = cut;
}

void paw3395_burst(struct paw3395_dev *dev, uint8_t *buf) {
	dev->read(REG_MOTION_BURST, buf, BURST_LEN);
}

void paw3395_shutdown(struct paw3395_dev *dev) {
	paw3395_write_reg(dev, REG_SHUTDOWN, 0xB6);
}