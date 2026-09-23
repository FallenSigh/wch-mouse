#include "CH58x_common.h"
#include "ch585_usbhs_device.h"
#include "UART.h"
#include "mouse.h"
#include "rgb.h"
#include "paw3395.h"

static volatile uint32_t s_tick_ms;

/* Default sensor resolution at boot (gaming-baseline CPI). The driver
 * clamps to its supported range [50, 26000]. */
#define MOUSE_DEFAULT_CPI   1600u

__INTERRUPT void TMR3_IRQHandler() {
    if (TMR3_GetITFlag(TMR0_3_IT_CYC_END)) {
        TMR3_ClearITFlag(TMR0_3_IT_CYC_END);
        s_tick_ms++;
    }
}

#define BIO_RST_PIN          GPIO_Pin_11
#define BIO_RST_ACTIVE_LOW   1

void bio_reset(void)
{
    GPIOA_ModeCfg(BIO_RST_PIN, GPIO_ModeOut_PP_5mA);
    GPIOA_ResetBits(BIO_RST_PIN);   /* assert reset  */
    mDelaymS(20);
    GPIOA_SetBits(BIO_RST_PIN);     /* release reset */
    mDelaymS(100);                  /* module boot time */
}


int main() {
    uint8_t len;

    HSECFG_Capacitance(HSECap_12p);
    SetSysClock(SYSCLK_FREQ);

    GPIOPinRemap(DISABLE, RB_RF_ANT_SW_EN);

    /* Release the BIO module from reset before expecting its UART traffic. */
    bio_reset();

    // init uart1
    GPIOA_SetBits(GPIO_Pin_9);
    GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(GPIO_Pin_9, GPIO_ModeOut_PP_5mA);
    UART1_DefInit();

    /* Tim2 init */
    TMR2_TimerInit(FREQ_SYS / 10000);
    TMR2_ITCfg(ENABLE, TMR0_3_IT_CYC_END);
    PFIC_EnableIRQ(TMR2_IRQn);

    /* Usart3 init */
    UART3_Init( 1, DEF_UARTx_BAUDRATE, DEF_UARTx_STOPBIT, DEF_UARTx_PARITY  );

    USBHS_Device_Init(ENABLE);
    PFIC_EnableIRQ( USB2_DEVICE_IRQn );

    mouse_init();

    // TIM3 at 1kHz for button debouncing
    TMR3_TimerInit(FREQ_SYS / 1000);
    TMR3_ITCfg(ENABLE, TMR0_3_IT_CYC_END);
    PFIC_EnableIRQ(TMR3_IRQn);

    rgb_init();
    paw3395_init();
    paw3395_set_cpi(800);
    printf("init done!\n");

    uint32_t last_btn_scan = 0;
    uint8_t data[12];
    while(1) {
        UART3_DataRx_Deal();
        UART3_DataTx_Deal();

        if (s_tick_ms - last_btn_scan >= 1) {
            last_btn_scan = s_tick_ms;
            mouse_scan(s_tick_ms);
        }

        /* RGB underglow: cycle the hue at ~30 Hz. The TIM1+DMA driver does
         * all the bit timing in hardware, so this loop just edits the
         * colour buffer and triggers a one-frame DMA refresh. No global
         * IRQ disable, no impact on USB. */
        static uint32_t last_rgb_ms = 0;
        static uint16_t rgb_hue    = 0;
        if (s_tick_ms - last_rgb_ms >= 33) {
            last_rgb_ms = s_tick_ms;
            rgb_set_brightness(1);
            uint8_t r, g, b;
            uint8_t region = (uint8_t)(rgb_hue / 60);
            uint8_t rem    = (uint8_t)((rgb_hue % 60) * 255U / 60U);
            switch (region) {
                case 0: r = 255;     g = rem;       b = 0;   break;
                case 1: r = 255 - rem; g = 255;       b = 0;   break;
                case 2: r = 0;       g = 255;       b = rem; break;
                case 3: r = 0;       g = 255 - rem; b = 255; break;
                case 4: r = rem;     g = 0;         b = 255; break;
                default: r = 255;    g = 0;         b = 255 - rem; break;
            }
            rgb_set_all(r, g, b);
            rgb_show();
            rgb_hue = (rgb_hue + 1) % 360;
        }
    }

    return 0;
}
