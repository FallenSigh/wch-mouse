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
#include <string.h>

#include "rf_cfg.h"
#include "CH58x_common.h"
#include "ch585_usbhs_device.h"
#include "log.h"
#include "wchrf.h"

#define HID_EP  DEF_UEP4

/* The vendor Feature report frame, and how many replies go out carrying a
 * captured one: the 63-byte frame takes 4 pieces, so 400 replies is ~100 passes
 * at 1 kHz - well under a tenth of a second, and the mouse only has to get one
 * pass through intact. */
#define RF_CMD_MAX     63u
#define RF_CMD_REPEAT  400u

static __attribute__((aligned(4))) uint8_t s_rx[RF_RX_BUF_LEN];   /* RF DMA target */
static RfPacket_t s_mail;       /* last complete packet, consumed by the poll */
static RfAck_t    s_ack;        /* what we DMA back to the mouse */
static rfipRx_t   s_rx_param;
static rfipTx_t   s_tx_param;

static volatile bool s_rx_ready;
static uint32_t      s_rx_n;

static uint8_t  s_cmd[RF_CMD_MAX];   /* the host's last SET frame */
static uint16_t s_cmd_len;           /* 0 when there is nothing to carry */
static uint16_t s_cmd_off;           /* next piece to hand out */
static uint16_t s_cmd_left;          /* replies still carrying it */
static uint8_t  s_cmd_id;            /* the mouse dedups repeats on this */
static volatile uint16_t s_cmd_trace; /* nonzero once a fresh frame arrived */
static volatile uint16_t s_cmd_prev_ok; /* the answer held when it did */

static uint8_t  s_reply[RF_CMD_MAX];      /* assembly for the current pass */
static uint8_t  s_reply_ok[RF_CMD_MAX];   /* the last whole answer: what a read-back serves */
static uint16_t s_reply_ok_len;           /* its length; 0 = nothing to serve */
static uint16_t s_reply_len;         /* pieces collected so far */
static uint16_t s_reply_total;       /* 0 until its header is in */
static uint8_t  s_reply_id;          /* the generation being collected */
static uint8_t  s_reply_ignore;      /* the generation the last command ruled out */
static uint8_t  s_reply_done;        /* the current answer is whole */
static uint32_t s_reply_chunks;      /* pieces taken for it */
static uint32_t s_reply_restarts;    /* passes that ended torn */

void proto_handle_set(const uint8_t *frame, uint16_t len);
void proto_handle_get(uint8_t *frame, uint16_t len);

/* Re-arm the receiver; each packet is one poll, so this has to be prompt. */
static void rf_rx_start(void)
{
    RFIP_SetRx(&s_rx_param);
}

/* The host writes the same vendor Feature report it writes to the mouse. The
 * dongle has no protocol module of its own, so it only captures the frame for
 * the RF side - the mouse is what runs it. Called from the USB ISR, so the copy
 * is the whole body. */
void proto_handle_set(const uint8_t *frame, uint16_t len)
{
    if ((frame == NULL) || (len == 0u)) {
        return;
    }

    s_cmd_len = (len > RF_CMD_MAX) ? RF_CMD_MAX : len;
    memcpy(s_cmd, frame, s_cmd_len);

    if (++s_cmd_id == 0u) {
        s_cmd_id = 1u;   /* 0 means "nothing pending" on the wire */
    }

    s_cmd_off  = 0u;
    s_cmd_left = RF_CMD_REPEAT;
    s_cmd_trace = s_cmd_len;

    /* The answer on hand belongs to the previous request, so a read-back would
     * hand it out as if it were this one's. Drop it, and ignore the re-sends of
     * it that are still in flight: GET_FEATURE then serves zeros until the
     * mouse's own answer has streamed in, which the host tells apart from a
     * real reply and waits out. */
    s_cmd_prev_ok  = s_reply_ok_len;   /* 0 means it never arrived at all */
    s_reply_ignore = s_reply_id;
    s_reply_id     = 0u;
    s_reply_len    = 0u;
    s_reply_total  = 0u;
    s_reply_ok_len = 0u;
    s_reply_done   = false;
}

/* Collect the mouse's answer out of the uplink frames it streams. A new
 * resp_id is the marker to start over, so a torn collection is discarded
 * rather than served. */
static void rf_reply_feed(const RfPacket_t *pkt)
{
    uint16_t off;
    uint16_t n;

    if ((pkt->resp_id == 0u) || (pkt->resp_id == s_reply_ignore)) {
        return;                      /* idle, or an answer already ruled out */
    }

    if (pkt->resp_id != s_reply_id) {
        s_reply_id       = pkt->resp_id;
        s_reply_len      = 0u;
        s_reply_total    = 0u;
        s_reply_done     = false;
        s_reply_chunks   = 0u;
        s_reply_restarts = 0u;
    }

    /* Chunk 0 starts a pass. The mouse re-sends the whole answer, so this is
     * the retry - and without dropping what a torn pass left behind, one lost
     * piece would wedge the collection until the next host command. */
    if (pkt->chunk == 0u) {
        if (s_reply_len != 0u) {
            s_reply_restarts++;
        }
        s_reply_len   = 0u;
        s_reply_total = 0u;
    }

    off = (uint16_t)pkt->chunk * RF_REPLY_CHUNK;
    if (off != s_reply_len) {
        return;                      /* a piece we are not waiting for */
    }

    n = (uint16_t)(RF_CMD_MAX - s_reply_len);
    if (n > RF_REPLY_CHUNK) {
        n = RF_REPLY_CHUNK;
    }

    memcpy(&s_reply[s_reply_len], pkt->reply, n);
    s_reply_len = (uint16_t)(s_reply_len + n);
    s_reply_chunks++;

    /* cmd + seq + len, payload, then sum8 - the same framing as the host side. */
    if ((s_reply_len >= 3u) && (s_reply[2] <= (uint8_t)(RF_CMD_MAX - 4u))) {
        s_reply_total = (uint16_t)(s_reply[2] + 4u);
    }

    /* Logged once per answer, and snapshot here for good: the mouse keeps
     * re-sending the whole thing, and each pass starts by clearing the assembly,
     * so without the snapshot a read-back would only ever catch it in the gap
     * between two passes. A nonzero restart count is the link tearing a pass up. */
    if (!s_reply_done && (s_reply_total != 0u) && (s_reply_len >= s_reply_total)) {
        s_reply_done   = true;
        s_reply_ok_len = s_reply_total;
        memcpy(s_reply_ok, s_reply, s_reply_total);
        LOG_I("RF", "reply id=%u chunks=%u restarts=%u", (unsigned)s_reply_id,
              (unsigned)s_reply_chunks, (unsigned)s_reply_restarts);
    }
}

/* GET_FEATURE on the host side. Zeros until a whole answer has arrived: the
 * caller's checksum then rejects it instead of passing off a torn frame. */
void proto_handle_get(uint8_t *frame, uint16_t len)
{
    if ((frame == NULL) || (len == 0u)) {
        return;
    }

    memset(frame, 0, len);
    if (s_reply_ok_len == 0u) {
        return;
    }

    memcpy(frame, s_reply_ok, (len < RF_CMD_MAX) ? len : RF_CMD_MAX);
}

/* Rotate the pending frame through the replies. Chunk 0 restarts the mouse's
 * assembly, so handing the whole frame out again is the retry. */
static void rf_ack_fill_cmd(void)
{
    uint16_t n;

    if ((s_cmd_len == 0u) || (s_cmd_left == 0u)) {
        s_ack.cmd_id  = 0u;
        s_ack.chunk   = 0u;
        s_ack.cmd_len = 0u;
        return;
    }

    if (s_cmd_off >= s_cmd_len) {
        s_cmd_off = 0u;
    }

    n = (uint16_t)(s_cmd_len - s_cmd_off);
    if (n > RF_CMD_CHUNK) {
        n = RF_CMD_CHUNK;
    }

    s_ack.cmd_id  = s_cmd_id;
    s_ack.chunk   = (uint8_t)(s_cmd_off / RF_CMD_CHUNK);
    s_ack.cmd_len = (uint8_t)s_cmd_len;

    memset(s_ack.payload, 0, sizeof(s_ack.payload));
    memcpy(s_ack.payload, &s_cmd[s_cmd_off], n);

    s_cmd_off = (uint16_t)(s_cmd_off + RF_CMD_CHUNK);
    s_cmd_left--;
}

static void rf_process_cb(rfRole_States_t sta, uint8_t id)
{
    (void)id;

    if (sta & RF_STATE_RX) {
        const RfPacket_t *pkt = (const RfPacket_t *)s_rx;

        s_mail     = *pkt;
        s_rx_ready = true;
        s_rx_n++;

        /* Answer straight away: the mouse is waiting for this inside the same
         * period it sent in, so the reply goes out from the callback and the
         * receiver is only re-armed once it has left. */
        s_ack.type     = RF_ACK_TYPE;
        s_ack.length   = RF_ACK_LEN;
        s_ack.rx_count = (uint16_t)s_rx_n;
        rf_ack_fill_cmd();

        RFIP_SetTxStart();
        RFIP_SetTxParm(&s_tx_param);
    }
    if ((sta & RF_STATE_TX_FINISH) || (sta & RF_STATE_TIMEOUT) || (sta & RF_STATE_RX_CRCERR)) {
        rf_rx_start();
    }
}

void rf_dongle_init(void)
{
    rfRoleConfig_t conf = {
        .TxPower     = LL_TX_POWEER_4_DBM,
        .rfProcessCB = rf_process_cb,
        .processMask = RF_STATE_RX | RF_STATE_RX_CRCERR | RF_STATE_TX_FINISH
                     | RF_STATE_TIMEOUT,
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

    s_tx_param.accessAddress = RF_ACCESS_ADDRESS;
    s_tx_param.crcInit       = RF_CRC_INIT;
    s_tx_param.frequency     = RF_CHANNEL;
    s_tx_param.properties    = LLE_MODE_PHY_2M;
    s_tx_param.sendCount     = 1;
    s_tx_param.txDMA         = (uint32_t)&s_ack;

    PFIC_EnableIRQ(BLEB_IRQn);
    PFIC_EnableIRQ(BLEL_IRQn);

    rf_rx_start();
    LOG_I("RF", "dongle rx up addr=%08X ch=%u", (unsigned)RF_ACCESS_ADDRESS,
          (unsigned)RF_CHANNEL);
}

void rf_dongle_poll(void)
{
    if (s_cmd_trace != 0u) {
        /* prev is the answer held when this command arrived: 0 there means the
         * previous one never made it back, which is the failure to chase. */
        LOG_I("RF", "host cmd %u bytes, prev reply %u", (unsigned)s_cmd_trace,
              (unsigned)s_cmd_prev_ok);
        s_cmd_trace   = 0u;
        s_cmd_prev_ok = 0u;
    }

    if (!s_rx_ready) {
        return;
    }
    s_rx_ready = false;

    if ((s_mail.type == RF_PKT_TYPE) && (s_mail.length == RF_PKT_LEN)) {
        USBHS_Endp_DataUp(HID_EP, (uint8_t *)&s_mail.report, sizeof(MouseReport_t),
                          DEF_UEP_CPY_LOAD);
        rf_reply_feed(&s_mail);

        /* Raw dump of the on-air alignment: type, length, seq, resv, then the 6
         * report bytes. Debug-only - it is one line per 5 s and stripped unless
         * the build asks for LOG_LVL_DEBUG. */
        if (s_rx_n <= 5u || (s_rx_n % 5000u) == 0u) {
            const uint8_t *p = (const uint8_t *)&s_mail;

            LOG_D("RF", "rx %u: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                  (unsigned)s_rx_n, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9]);
        }
    } else {
        const uint8_t *p = (const uint8_t *)&s_mail;

        LOG_W("RF", "rx bad type=%02X len=%u raw %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
              p[0], (unsigned)p[1], p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9]);
    }
}
