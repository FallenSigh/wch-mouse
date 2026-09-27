#include "bmi270_port.h"
#include <stdint.h>
#include "CH58x_common.h"
#include "CH58x_gpio.h"
#include "bmi270.h"
#include "bmi2_defs.h"

#define PIN_IMU_SCLK GPIO_Pin_0
#define PIN_IMU_MOSI GPIO_Pin_1
#define PIN_IMU_MISO GPIO_Pin_2
#define PIN_IMU_CS   GPIO_Pin_10
#define PIN_IMU_INT  GPIO_Pin_6

static inline void cs_low(void)  { GPIOA_ResetBits(PIN_IMU_CS); }
static inline void cs_high(void) { GPIOA_SetBits(PIN_IMU_CS); }

#define SPI_READ_BIT   0x80u
#define SPI_WRITE_MASK 0x7Fu

static BMI2_INTF_RETURN_TYPE bmi270_read_reg(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr __unused) {   
    cs_low();
    SPI1_MasterSendByte((uint8_t)(reg_addr | 0x80));
    SPI1_MasterRecv(reg_data, len);
    cs_high();
    return BMI2_OK;
}

static BMI2_INTF_RETURN_TYPE bmi270_write_reg(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr __unused) {
    cs_low();
    SPI1_MasterSendByte((uint8_t)(reg_addr & 0x7F));
    SPI1_MasterTrans((uint8_t *)reg_data, len);
    cs_high();
    return BMI2_OK;
}

static void bmi270_delay_us(uint32_t period, void *intf_ptr) {
    DelayUs(period);
}

int8_t bmi270_port_init(struct bmi2_dev *bmi) {
    GPIOA_SetBits(PIN_IMU_SCLK | PIN_IMU_MOSI | PIN_IMU_CS);
    GPIOA_ModeCfg(PIN_IMU_SCLK | PIN_IMU_MOSI | PIN_IMU_CS, GPIO_ModeOut_PP_5mA);
    GPIOA_ModeCfg(PIN_IMU_MISO, GPIO_ModeIN_Floating);
    GPIOA_ModeCfg(PIN_IMU_INT,  GPIO_ModeIN_Floating);

    SPI1_MasterDefInit();
    SPI1_DataMode(Mode0_HighBitINFront);
    SPI1_CLKCfg(8);  

    bmi->intf = BMI2_SPI_INTF;
    bmi->read = bmi270_read_reg;
    bmi->write = bmi270_write_reg;
    bmi->delay_us = bmi270_delay_us;
    bmi->intf_ptr = (void*)0;
    bmi->read_write_len = 32;
    bmi->config_file_ptr = NULL;

    return bmi270_init(bmi);
}