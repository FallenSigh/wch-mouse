/*
 * src/transport_rf.c - 2.4G proprietary RF transport (device/TX side).
 *
 * Ported from the WCH RF_Basic example (docs/CH585EVT/EVT/EXAM/BLE/RF_Basic).
 * The mouse is the RF "device" that blasts one MouseReport per millisecond;
 * a companion dongle (host/RX) receives and forwards it as USB HID.
 *
 * RF and BLE share the radio, so this backend switches the radio to
 * RFIP_MODE_RF_BASIC and must not be used together with the BLE backend.
 */

#include "transport_rf.h"

#include <stddef.h>

#ifdef WCH_RF_ENABLE

#include "rf_cfg.h"
#include "radio_mode.h"
#include "CH58x_common.h"
#include "log.h"
#include "wchrf.h"

/* One report per millisecond, matching the host-side USB poll. */
#define RF_TX_PERIOD_MS   1u

static MouseReport_t s_acc;        /* motion accumulated since the last packet */
static RfPacket_t    s_tx;         /* what the RFIP DMAs out - kept separate from
                                    * s_acc so the async DMA never races the reset */
static bool       s_have;
static uint8_t    s_seq;
static uint32_t   s_tx_n;
static uint32_t   s_tx_ms;
static bool       s_up;
static uint32_t   s_tx_count;
static uint32_t   s_stat_ms;

static rfipTx_t   s_tx_param;

static void rf_process_cb(rfRole_States_t sta, uint8_t id)
{
    (void)id;

    if (sta & RF_STATE_TX_FINISH) {
        s_tx_count++;
    }
}

static bool rf_init(void)
{
    /* One radio, one live transport: when this boot selected BLE the RF side
     * stays uninitialised and its link_up() never comes up. */
    if (!radio_mode_is(RADIO_MODE_RF)) {
        return false;
    }

    rfRoleConfig_t conf = {
        .TxPower     = LL_TX_POWEER_4_DBM,
        .rfProcessCB = rf_process_cb,
        .processMask = RF_STATE_TX_FINISH | RF_STATE_TIMEOUT,
    };

    if (RFRole_SwitchMode(RFIP_MODE_RF_BASIC) != SUCCESS) {
        LOG_E("RF", "SwitchMode failed");
        return false;
    }
    RFRole_BasicInit(&conf);

    rfRoleParam_t parm = { 0 };

    parm.accessAddress = RF_ACCESS_ADDRESS;
    parm.crcInit       = RF_CRC_INIT;
    parm.properties    = LLE_MODE_PHY_2M;
    parm.sendInterval  = 1999 * 2;
    parm.sendTime      = 20 * 2;
    RFRole_SetParam(&parm);

    s_tx_param.accessAddress = RF_ACCESS_ADDRESS;
    s_tx_param.crcInit       = RF_CRC_INIT;
    s_tx_param.frequency     = RF_CHANNEL;
    s_tx_param.properties    = LLE_MODE_PHY_2M;
    s_tx_param.sendCount     = 1;
    s_tx_param.txDMA         = (uint32_t)&s_tx;

    PFIC_EnableIRQ(BLEB_IRQn);
    PFIC_EnableIRQ(BLEL_IRQn);

    s_up = true;
    LOG_I("RF", "init addr=%08X ch=%u", (unsigned)RF_ACCESS_ADDRESS, (unsigned)RF_CHANNEL);
    return true;
}

static bool rf_link_up(void)
{
    return s_up;
}

static bool rf_send(const MouseReport_t *rpt)
{
    if (!s_up) {
        return false;
    }

    /* Buttons are level, motion is relative: keep the latest buttons/wheel and
     * add the motion, so packets lost on air do not lose motion. */
    s_acc.buttons = rpt->buttons;
    s_acc.dx      = (int16_t)(s_acc.dx + rpt->dx);
    s_acc.dy      = (int16_t)(s_acc.dy + rpt->dy);
    s_acc.wheel   = (int8_t)(s_acc.wheel + rpt->wheel);
    s_have        = true;
    return true;
}

static void rf_poll(uint32_t now_ms)
{
    if (!s_up) {
        return;
    }

    if (now_ms - s_stat_ms >= 5000u) {
        s_stat_ms = now_ms;
        LOG_I("RF", "tx %u in 5s", (unsigned)s_tx_count);
        s_tx_count = 0;
    }

    if (!s_have || (now_ms - s_tx_ms < RF_TX_PERIOD_MS)) {
        return;
    }
    s_tx_ms = now_ms;

    s_tx.type   = RF_PKT_TYPE;
    s_tx.length = RF_PKT_LEN;
    s_tx.seq    = ++s_seq;
    s_tx.resv   = 0;
    s_tx.report = s_acc;

    /* Reset the accumulator (a different buffer, so the RFIP's DMA can still
     * read s_tx safely) and hand the packet to the radio. */
    s_acc.buttons = 0;
    s_acc.dx      = 0;
    s_acc.dy      = 0;
    s_acc.wheel   = 0;
    s_have        = false;

    RFIP_SetTxStart();
    RFIP_SetTxParm(&s_tx_param);

    ++s_tx_n;
    if (s_tx_n <= 5u || (s_tx_n % 500u) == 0u) {
        const uint8_t *p = (const uint8_t *)&s_tx;

        LOG_I("RF", "tx %u: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
              (unsigned)s_tx_n, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9]);
    }
}

const transport_t transport_rf = {
    .id      = TR_RF24,
    .name    = "rf",
    .init    = rf_init,
    .link_up = rf_link_up,
    .send    = rf_send,
    .poll    = rf_poll,
};

#else  /* WCH_RF_ENABLE */

const transport_t transport_rf = {
    .id      = TR_RF24,
    .name    = "rf",
    .init    = NULL,
    .link_up = NULL,
    .send    = NULL,
    .poll    = NULL,
};

#endif /* WCH_RF_ENABLE */
