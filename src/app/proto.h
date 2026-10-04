#ifndef __PROTO_H__
#define __PROTO_H__

#include <stdbool.h>
#include <stdint.h>

/* Vendor configuration channel over a HID Feature report (usage page 0xFF00,
 * Report ID 0x02, 63 data bytes; the Report ID byte leads the control transfer).
 *
 * HID gives the host no payload on a GET_FEATURE, so a transaction is: the
 * host writes a request with SET_FEATURE, and reads the answer back with
 * GET_FEATURE. The device therefore keeps the last response.
 *
 * Frame (little-endian):
 *   [0]     cmd                 (bit 7 set on a response)
 *   [1]     seq                 (echoed by the device)
 *   [2]     len                 (payload bytes, <= PROTO_FRAME_LEN - 4)
 *   [3..]   payload
 *   [3+len] sum8                (sum of bytes 0..2+len)
 *   rest    0
 * The reply's payload starts with a status byte. */

#define PROTO_FRAME_LEN 63u

/* Prepare the channel (the configuration lives in settings.c). */
void proto_init(void);

/* SET_FEATURE (host -> device): parse, execute, fill the pending response. */
void proto_handle_set(const uint8_t *frame, uint16_t len);

/* GET_FEATURE (device -> host): copy the pending response into `frame`. */
void proto_handle_get(uint8_t *frame, uint16_t len);

/* The pending response itself, for a transport that has to carry it back on its
 * own (the RF link). `proto_response_gen()` changes with every reply, so the
 * caller can tell a fresh one from the one it already sent. */
const uint8_t *proto_response(void);
uint8_t proto_response_gen(void);

#endif /* __PROTO_H__ */
