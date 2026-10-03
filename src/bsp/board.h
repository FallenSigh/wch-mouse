/*
 * src/bsp/board.h - single source of truth for the CH585 mouse board wiring.
 *
 * Mirrors docs/io.csv. This is the one place to edit board pin assignments:
 * the macro value is the pin bit mask, and the port is implied by the caller's
 * GPIOA_* / GPIOB_* accessors. The trailing comment names the CH585 port and
 * the net, which is what disambiguates same-numbered pins on different ports
 * (e.g. BIO_RST PA11 vs BTN_SIDE1 PB11, BAT_PGOOD PB6 vs IMU_INT PA6).
 */

#ifndef __BOARD_H__
#define __BOARD_H__

#include "CH58x_common.h"

/* Buttons */
#define BOARD_BTN_LEFT_PIN  GPIO_Pin_7  /* PB7  PR_SW1 */
#define BOARD_BTN_RIGHT_PIN GPIO_Pin_1  /* PB1  PR_SW2 */
#define BOARD_BTN_MID_PIN   GPIO_Pin_9  /* PB9  PR_SW3 */
#define BOARD_BTN_SIDE1_PIN GPIO_Pin_11 /* PB11 PR_SW4 (Back) */
#define BOARD_BTN_SIDE2_PIN GPIO_Pin_4  /* PB4  PR_SW5 (Forward) */

/* Encoder */
#define BOARD_ENC_A_PIN GPIO_Pin_8  /* PB8  ENC_A */
#define BOARD_ENC_B_PIN GPIO_Pin_19 /* PB19 ENC_B (cannot wake) */

/* Battery */
#define BOARD_BAT_PGOOD_PIN GPIO_Pin_6 /* PB6  BT_PGOOD */
#define BOARD_BAT_CHG_PIN   GPIO_Pin_5 /* PB5  BT_CHG */
#define BOARD_BAT_VOL_PIN   GPIO_Pin_3 /* PA3  BT_VOL (ADC AIN6) */

/* Motor */
#define BOARD_MOTOR_PIN GPIO_Pin_0 /* PB0  MOTOR */

/* PAW3395 (SPI0) */
#define BOARD_PAW_CS_PIN         GPIO_Pin_12 /* PA12 MS_CS */
#define BOARD_PAW_SCLK_PIN       GPIO_Pin_13 /* PA13 MS_SCLK */
#define BOARD_PAW_MOSI_PIN       GPIO_Pin_14 /* PA14 MS_MOSI */
#define BOARD_PAW_MISO_PIN       GPIO_Pin_15 /* PA15 MS_MISO */
#define BOARD_PAW_MOTION_PIN     GPIO_Pin_17 /* PB17 MS_MOTION */
#define BOARD_PAW_MOTION_ALT_PIN GPIO_Pin_7  /* PA7  MS_MOTION_ALT (IRQ) */
#define BOARD_PAW_RST_PIN        GPIO_Pin_18 /* PB18 MS_RST */

/* BMI270 (SPI1) */
#define BOARD_IMU_SCLK_PIN GPIO_Pin_0  /* PA0  IMU_SCLK */
#define BOARD_IMU_MOSI_PIN GPIO_Pin_1  /* PA1  IMU_MOSI */
#define BOARD_IMU_MISO_PIN GPIO_Pin_2  /* PA2  IMU_MISO */
#define BOARD_IMU_CS_PIN   GPIO_Pin_10 /* PA10 IMU_CS */
#define BOARD_IMU_INT_PIN  GPIO_Pin_6  /* PA6  IMU_INT1 */

/* OLED / I2C */
#define BOARD_OLED_EN_PIN  GPIO_Pin_3  /* PB3  panel rail switch */
#define BOARD_OLED_RST_PIN GPIO_Pin_16 /* PB16 active low */
#define BOARD_I2C_SDA_PIN  GPIO_Pin_20 /* PB20 OLED_SDA */
#define BOARD_I2C_SCL_PIN  GPIO_Pin_21 /* PB21 OLED_SCL */

/* RGB (WS2812 / TIM1_CH1) */
#define BOARD_RGB_DIN_PIN GPIO_Pin_10 /* PB10 RGB_DIN */
#define BOARD_RGB_EN_PIN  GPIO_Pin_2  /* PB2  RGB_EN */

/* BIO (UART3) */
#define BOARD_BIO_RST_PIN GPIO_Pin_11 /* PA11 active low */
#define BOARD_BIO_TX_PIN  GPIO_Pin_5  /* PA5  BIO_TX (UART3 TXD) */
#define BOARD_BIO_RX_PIN  GPIO_Pin_4  /* PA4  BIO_RX (UART3 RXD) */

/* Debug UART1 */
#define BOARD_UART1_RX_PIN GPIO_Pin_8 /* PA8 */
#define BOARD_UART1_TX_PIN GPIO_Pin_9 /* PA9 */

/* ISP/BOOT strap */
#define BOARD_ISP_BTN_PIN GPIO_Pin_22 /* PB22 */

/* Input masks (must stay the exact bit combinations the buttons/encoder use). */
#define BOARD_INPUT_ALL_PINS                                                              \
    (BOARD_BTN_LEFT_PIN | BOARD_BTN_RIGHT_PIN | BOARD_BTN_MID_PIN | BOARD_BTN_SIDE1_PIN | \
     BOARD_BTN_SIDE2_PIN | BOARD_ENC_A_PIN | BOARD_ENC_B_PIN)

/* Standby wake-on-input. Only PB0-PB15 carry a GPIOB interrupt bit, so the
 * encoder's B phase (PB19) cannot be a wake source. */
#define BOARD_INPUT_WAKE_PINS                                                  \
    ((uint16_t)(BOARD_BTN_LEFT_PIN | BOARD_BTN_RIGHT_PIN | BOARD_BTN_MID_PIN | \
                BOARD_BTN_SIDE1_PIN | BOARD_BTN_SIDE2_PIN | BOARD_ENC_A_PIN))

#endif /* __BOARD_H__ */
