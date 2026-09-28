/*
 * src/display/i2c_bus.c - blocking master I2C for the display bus.
 *
 * The CH585 I2C defaults to PB13/PB12, which this board uses for USB HS, so
 * init sets RB_PIN_I2C to move SCL_/SDA_ to PB21/PB20 (docs/io.csv). The
 * peripheral is event/flag based; every wait here is bounded and a failed
 * transfer runs the vendor's recovery sequence, otherwise a single NACK
 * leaves the bus wedged for good.
 */

#include "i2c_bus.h"

#include <stddef.h>

#include "CH58x_common.h"
#include "log.h"

/* 400 kHz fast mode: SDA/SCL carry external 4.7 kOhm pull-ups to 3.3 V, so
 * the rise time is short enough. A board without them only has the CH585's
 * internal 20-50 kOhm, whose ~9 us rise time barely fits even 100 kHz - drop
 * I2C_CLOCK_HZ and raise I2C_RETRIES there. */
#define I2C_CLOCK_HZ     400000u
#define I2C_OWN_ADDR     0x42u
#define I2C_SPIN_LIMIT   200000u   /* bounded polls; a few ms worst case */
#define I2C_RETRIES      1u        /* pull-ups fitted: no retries expected */

#define I2C_7BIT_MAX     0x7Fu

/* An unrecovered NACK leaves SDA held and the state machine stuck, so reset
 * the peripheral and re-init after every failed transfer. */
static void i2c_bus_recover(void)
{
    I2C_GenerateSTOP(ENABLE);
    I2C_ClearFlag(I2C_FLAG_AF);

    I2C_SoftwareResetCmd(ENABLE);
    I2C_SoftwareResetCmd(DISABLE);

    I2C_Init(I2C_Mode_I2C, I2C_CLOCK_HZ, I2C_DutyCycle_16_9,
             I2C_Ack_Enable, I2C_AckAddr_7bit, I2C_OWN_ADDR);
    I2C_Cmd(ENABLE);
}

static bool i2c_wait_flag(uint32_t flag)
{
    uint32_t spin = I2C_SPIN_LIMIT;

    while (I2C_GetFlagStatus(flag) == RESET) {
        if (spin-- == 0u) {
            return false;
        }
    }

    return true;
}

void i2c_bus_init(void)
{
    /* SCL_/SDA_ on PB21/PB20 instead of the default PB13/PB12 (USB HS). */
    GPIOPinRemap(ENABLE, RB_PIN_I2C);
    GPIOB_ModeCfg(GPIO_Pin_20 | GPIO_Pin_21, GPIO_ModeIN_PU);

    I2C_Init(I2C_Mode_I2C, I2C_CLOCK_HZ, I2C_DutyCycle_16_9,
             I2C_Ack_Enable, I2C_AckAddr_7bit, I2C_OWN_ADDR);
    I2C_Cmd(ENABLE);

    LOG_I("I2C", "master up, SCL/SDA on PB21/PB20");
}

/* Send the address byte and report whether the slave ACKed. The success path
 * goes through I2C_CheckEvent because that reads STAR1 *and* STAR2, the
 * sequence the peripheral needs to clear ADDR - I2C_GetFlagStatus alone only
 * touches one register, so ADDR would stay set and the TXE loop could never
 * start. AF (acknowledge failure) is how a missing device shows up. */
static bool i2c_send_address(uint8_t addr7)
{
    uint32_t spin = I2C_SPIN_LIMIT;

    I2C_Send7bitAddress((uint8_t)(addr7 << 1), I2C_Direction_Transmitter);

    while (spin-- != 0u) {
        if (I2C_CheckEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) {
            return true;
        }
        if (I2C_GetFlagStatus(I2C_FLAG_AF) != RESET) {
            return false;
        }
    }

    return false;
}

bool i2c_bus_probe(uint8_t addr7)
{
    bool present;

    if (addr7 > I2C_7BIT_MAX) {
        return false;
    }

    I2C_GenerateSTART(ENABLE);
    if (!i2c_wait_flag(I2C_FLAG_SB)) {
        i2c_bus_recover();
        return false;
    }

    present = i2c_send_address(addr7);

    I2C_GenerateSTOP(ENABLE);
    I2C_ClearFlag(I2C_FLAG_AF);

    return present;
}

/* One transaction: optional leading byte, then len payload bytes, then STOP.
 * Reports failure without recovering - the callers retry. */
static bool i2c_write_transfer(uint8_t addr7, bool with_prefix, uint8_t prefix,
                               const uint8_t *data, uint16_t len)
{
    I2C_GenerateSTART(ENABLE);
    if (!i2c_wait_flag(I2C_FLAG_SB)) {
        LOG_E("I2C", "no start s1=%04X s2=%04X", (unsigned)R16_I2C_STAR1, (unsigned)R16_I2C_STAR2);
        i2c_bus_recover();
        return false;
    }

    if (!i2c_send_address(addr7)) {
        LOG_E("I2C", "addr 0x%02X nack s1=%04X s2=%04X", (unsigned)addr7,
              (unsigned)R16_I2C_STAR1, (unsigned)R16_I2C_STAR2);
        i2c_bus_recover();
        return false;
    }

    if (with_prefix) {
        if (!i2c_wait_flag(I2C_FLAG_TXE)) {
            LOG_E("I2C", "no txe for ctrl s1=%04X s2=%04X", (unsigned)R16_I2C_STAR1,
                  (unsigned)R16_I2C_STAR2);
            i2c_bus_recover();
            return false;
        }
        I2C_SendData(prefix);
    }

    for (uint16_t i = 0u; i < len; i++) {
        if (!i2c_wait_flag(I2C_FLAG_TXE)) {
            LOG_E("I2C", "no txe at %u s1=%04X s2=%04X", (unsigned)i,
                  (unsigned)R16_I2C_STAR1, (unsigned)R16_I2C_STAR2);
            i2c_bus_recover();
            return false;
        }
        I2C_SendData(data[i]);
    }

    if (!i2c_wait_flag(I2C_FLAG_BTF)) {
        LOG_E("I2C", "no btf s1=%04X s2=%04X", (unsigned)R16_I2C_STAR1, (unsigned)R16_I2C_STAR2);
        i2c_bus_recover();
        return false;
    }

    I2C_GenerateSTOP(ENABLE);
    return true;
}

bool i2c_bus_write(uint8_t addr7, const uint8_t *data, uint16_t len)
{
    if (addr7 > I2C_7BIT_MAX || data == NULL || len == 0u) {
        LOG_E("I2C", "bad args addr=0x%02X len=%u", (unsigned)addr7, (unsigned)len);
        return false;
    }

    for (uint8_t attempt = 0u; attempt < I2C_RETRIES; attempt++) {
        if (i2c_write_transfer(addr7, false, 0u, data, len)) {
            return true;
        }
        LOG_W("I2C", "write retry %u", (unsigned)attempt);
    }

    return false;
}

bool i2c_bus_write_reg(uint8_t addr7, uint8_t reg, const uint8_t *data, uint16_t len)
{
    if (addr7 > I2C_7BIT_MAX || data == NULL || len == 0u) {
        LOG_E("I2C", "bad args addr=0x%02X len=%u", (unsigned)addr7, (unsigned)len);
        return false;
    }

    for (uint8_t attempt = 0u; attempt < I2C_RETRIES; attempt++) {
        if (i2c_write_transfer(addr7, true, reg, data, len)) {
            return true;
        }
        LOG_W("I2C", "write_reg retry %u", (unsigned)attempt);
    }

    return false;
}
