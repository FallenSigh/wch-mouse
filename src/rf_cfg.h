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

#endif /* __RF_CFG_H__ */
