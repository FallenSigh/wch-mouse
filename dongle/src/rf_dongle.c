/*
 * src/rf_dongle.c - 2.4G RF host (RX) for the wch-mouse dongle.
 *
 * Ported from the WCH RF_Basic example. The dongle keeps the RF IP armed to
 * receive, and forwards each mouse report to the PC as a USB HID report. The
 * on-air format and RF parameters live in the shared wch-mouse src/rf_cfg.h so
 * both ends stay in sync.
 */

#include "rf_dongle.h"

#include <stdbool.h>

#include "rf_cfg.h"
#include "CH58x_common.h"
#include "ch585_usbhs_device.h"
#include "log.h"
#include "wchrf.h"

#define HID_EP  DEF_UEP4

static RfPacket_t s_rx;         /* RF DMA target */
static RfPacket_t s_mail;       /* last complete packet, consumed by the poll */
static rfipRx_t   s_rx_param;

static volatile bool s_rx_ready;
static uint32_t      s_rx_n;

/* Re-arm the receiver; each packet is one poll, so this has to be prompt. */
static void rf_rx_start(void)
{
    RFIP_SetRx(&s_rx_param);
}

static void rf_process_cb(rfRole_States_t sta, uint8_t id)
{
    (void)id;

    if (sta & RF_STATE_RX) {
        s_mail     = s_rx;
        s_rx_ready = true;
        rf_rx_start();
    }
    if (sta & RF_STATE_RX_CRCERR) {
        rf_rx_start();
    }
}

void rf_dongle_init(void)
{
    rfRoleConfig_t conf = {
        .TxPower     = LL_TX_POWEER_4_DBM,
        .rfProcessCB = rf_process_cb,
        .processMask = RF_STATE_RX | RF_STATE_RX_CRCERR,
    };

    RFRole_SwitchMode(RFIP_MODE_RF_BASIC);
    RFRole_BasicInit(&conf);

    rfRoleParam_t parm = { 0 };

    parm.accessAddress = RF_ACCESS_ADDRESS;
    parm.crcInit       = RF_CRC_INIT;
    parm.properties    = LLE_MODE_PHY_2M;
    parm.sendInterval  = 1999 * 2;
    parm.sendTime      = 20 * 2;
    RFRole_SetParam(&parm);

    s_rx_param.accessAddress = RF_ACCESS_ADDRESS;
    s_rx_param.crcInit       = RF_CRC_INIT;
    s_rx_param.frequency     = RF_CHANNEL;
    s_rx_param.properties    = LLE_MODE_PHY_2M;
    s_rx_param.rxDMA         = (uint32_t)&s_rx;
    s_rx_param.rxMaxLen      = RF_RX_MAX_LEN;
    s_rx_param.timeOut       = 0;

    PFIC_EnableIRQ(BLEB_IRQn);
    PFIC_EnableIRQ(BLEL_IRQn);

    rf_rx_start();
    LOG_I("RF", "dongle rx up addr=%08X ch=%u", (unsigned)RF_ACCESS_ADDRESS,
          (unsigned)RF_CHANNEL);
}

void rf_dongle_poll(void)
{
    if (!s_rx_ready) {
        return;
    }
    s_rx_ready = false;

    if ((s_mail.type == RF_PKT_TYPE) && (s_mail.length == RF_PKT_LEN)) {
        USBHS_Endp_DataUp(HID_EP, (uint8_t *)&s_mail.report, sizeof(MouseReport_t),
                          DEF_UEP_CPY_LOAD);

        /* Throttled raw dump (~every 0.5 s at 1 kHz) to check the on-air
         * alignment: type, length, seq, resv, then the 6 report bytes. */
        ++s_rx_n;
        if (s_rx_n <= 5u || (s_rx_n % 500u) == 0u) {
            const uint8_t *p = (const uint8_t *)&s_mail;

            LOG_I("RF", "rx %u: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                  (unsigned)s_rx_n, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9]);
        }
    } else {
        const uint8_t *p = (const uint8_t *)&s_mail;

        LOG_W("RF", "rx bad type=%02X len=%u raw %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
              p[0], (unsigned)p[1], p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9]);
    }
}
