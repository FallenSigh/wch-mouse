#ifndef __RF_CFG_H__
#define __RF_CFG_H__

#include <stdint.h>

#include "mouse.h"

/* Shared 2.4G RF link configuration. The mouse (device/TX) and the dongle
 * (host/RX) MUST use identical values, and the on-air packet must track
 * MouseReport_t, so this header is shared by both projects.
 *
 * The on-air frame mirrors the RFIP header `rfPackage_t` (type, length, seq,
 * resv) followed by the report payload. */
#define RF_ACCESS_ADDRESS   0x71762345u
#define RF_CRC_INIT         0x555555u
#define RF_CHANNEL          16u          /* rfip channel index (cf. RF_Basic) */

#define RF_PKT_TYPE         0x55u        /* frame type / device id */

typedef struct __attribute__((packed)) {
    uint8_t       type;      /* RF_PKT_TYPE */
    uint8_t       length;    /* total frame length, incl. this 4-byte header */
    uint8_t       seq;       /* increments per packet; lets the dongle drop dups */
    uint8_t       resv;
    MouseReport_t report;
} RfPacket_t;

#define RF_PKT_LEN          (sizeof(RfPacket_t))
#define RF_RX_MAX_LEN       RF_PKT_LEN

/* Reverse direction: the dongle answers every packet it receives with one of
 * these, so the mouse gets a channel back without a hardware ACK (the RFIP
 * exposes no way to load an ACK payload).
 *
 * It doubles as the command channel. The host writes the same vendor Feature
 * report it sends to the mouse; the dongle cannot run that protocol itself, so
 * it captures the frame and rotates it through the replies in RF_CMD_CHUNK
 * pieces. The mouse restarts its assembly on every chunk 0, which is the retry
 * - a dropped reply only costs another pass - and cmd_id is what stops a repeat
 * from executing twice (a BIO acquisition blocks the mouse for 50 ms). */
#define RF_ACK_TYPE         0x7Eu        /* deliberately not RF_PKT_LEN */
#define RF_CMD_CHUNK        16u          /* command bytes carried per reply */

typedef struct __attribute__((packed)) {
    uint8_t       type;      /* RF_ACK_TYPE */
    uint8_t       length;    /* total frame length, incl. this 4-byte header */
    uint8_t       cmd_id;    /* 0 when idle; changes per captured command */
    uint8_t       chunk;     /* 0-based index of this piece */
    uint8_t       cmd_len;   /* whole command length; 0 when idle */
    uint16_t      rx_count;  /* packets received so far; the mouse differences it */
    uint8_t       payload[RF_CMD_CHUNK];
} RfAck_t;

#define RF_ACK_LEN          (sizeof(RfAck_t))

/* Both ends must point the RFIP's RX DMA at a buffer this size. It is NOT the
 * frame length: the vendor example documents a 264-byte minimum whatever
 * rxMaxLen is, and a buffer sized for the frame lets the DMA run past it. That
 * stays invisible until the tail lands on something live - sizing the reply
 * buffer for its 6 bytes put ACK bytes into the TX buffer, which showed up as
 * the cursor drifting steadily in one direction. */
#define RF_RX_BUF_LEN       264u

#endif /* __RF_CFG_H__ */
