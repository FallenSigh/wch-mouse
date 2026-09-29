/*
 * src/app/proto.c - vendor configuration channel over a HID Feature report.
 *
 * See proto.h for the frame layout and the SET-then-GET transaction. Frames
 * arrive from the USB EP0 handler (ch585_usbhs_device.c); the module is
 * transport-agnostic so the same table can be driven over BLE later.
 */

#include "proto.h"

#include <string.h>

#include "bat.h"
#include "bio.h"
#include "paw3395.h"
#include "radio_mode.h"
#include "settings.h"

#define PROTO_SUM_OFF(frame, len)  ((uint16_t)(3u + (len)))

/* Command codes; bit 7 marks the reply. */
#define PROTO_CMD_GET_VERSION   0x01u
#define PROTO_CMD_GET_INFO      0x02u
#define PROTO_CMD_GET_RADIO     0x03u
#define PROTO_CMD_GET_DPI       0x10u
#define PROTO_CMD_SET_DPI       0x11u
#define PROTO_CMD_GET_SENSOR    0x12u
#define PROTO_CMD_SET_SENSOR    0x13u
#define PROTO_CMD_GET_LIFT      0x14u
#define PROTO_CMD_SET_LIFT      0x15u
#define PROTO_CMD_GET_BATTERY   0x24u
#define PROTO_CMD_BIO_ACQ       0x50u
#define PROTO_CMD_BIO_SLEEP     0x51u

/* Status byte (first payload byte of every reply). */
#define PROTO_ST_OK           0x00u
#define PROTO_ST_UNKNOWN      0x01u
#define PROTO_ST_BADLEN       0x02u
#define PROTO_ST_BADSUM       0x03u
#define PROTO_ST_UNSUPPORTED  0x04u

#define PROTO_VERSION         1u

static uint8_t s_resp[PROTO_FRAME_LEN];

static uint8_t proto_sum(const uint8_t *p, uint16_t n)
{
    uint8_t sum = 0u;

    for (uint16_t i = 0u; i < n; i++) {
        sum = (uint8_t)(sum + p[i]);
    }

    return sum;
}

/* Build a reply: cmd|0x80, echo seq, payload = status + data. */
static void proto_reply(uint8_t cmd, uint8_t seq, uint8_t status, const uint8_t *data, uint8_t n)
{
    const uint8_t plen = (uint8_t)(n + 1u);

    memset(s_resp, 0, sizeof(s_resp));
    s_resp[0] = (uint8_t)(cmd | 0x80u);
    s_resp[1] = seq;
    s_resp[2] = plen;
    s_resp[3] = status;

    if ((data != NULL) && (n > 0u)) {
        memcpy(&s_resp[4], data, n);
    }

    s_resp[PROTO_SUM_OFF(s_resp, plen)] = proto_sum(s_resp, PROTO_SUM_OFF(s_resp, plen));
}

void proto_init(void)
{
    proto_reply(PROTO_CMD_GET_VERSION, 0u, PROTO_ST_OK, NULL, 0u);
}

void proto_handle_set(const uint8_t *frame, uint16_t len)
{
    uint8_t  cmd;
    uint8_t  seq;
    uint8_t  plen;

    if ((frame == NULL) || (len < 4u)) {
        return;
    }

    cmd  = frame[0];
    seq  = frame[1];
    plen = frame[2];

    if ((uint16_t)(PROTO_SUM_OFF(frame, plen)) > len) {
        proto_reply(cmd, seq, PROTO_ST_BADLEN, NULL, 0u);
        return;
    }

    if (frame[PROTO_SUM_OFF(frame, plen)] != proto_sum(frame, PROTO_SUM_OFF(frame, plen))) {
        proto_reply(cmd, seq, PROTO_ST_BADSUM, NULL, 0u);
        return;
    }

    switch (cmd) {
        case PROTO_CMD_GET_VERSION: {
            const uint8_t data[4] = { PROTO_VERSION, 1u, 0u, 0u };
            proto_reply(cmd, seq, PROTO_ST_OK, data, sizeof(data));
            break;
        }

        case PROTO_CMD_GET_INFO: {
            /* bit0 sensor, bit1 radio, bit2 battery, bit3 BIO. */
            const uint8_t data[2] = { 0x0Fu, 0x00u };
            proto_reply(cmd, seq, PROTO_ST_OK, data, sizeof(data));
            break;
        }

        case PROTO_CMD_GET_RADIO: {
            const uint8_t data[1] = { (uint8_t)radio_mode_get() };
            proto_reply(cmd, seq, PROTO_ST_OK, data, sizeof(data));
            break;
        }

        case PROTO_CMD_GET_DPI: {
            const uint16_t cpi = settings_get()->cpi;
            const uint8_t  data[2] = { (uint8_t)(cpi & 0xFFu), (uint8_t)(cpi >> 8) };
            proto_reply(cmd, seq, PROTO_ST_OK, data, sizeof(data));
            break;
        }

        case PROTO_CMD_SET_DPI: {
            if (plen < 2u) {
                proto_reply(cmd, seq, PROTO_ST_BADLEN, NULL, 0u);
                break;
            }

            settings_set_cpi((uint16_t)(frame[3] | (frame[4] << 8)));

            const uint16_t cpi = settings_get()->cpi;
            const uint8_t  data[2] = { (uint8_t)(cpi & 0xFFu), (uint8_t)(cpi >> 8) };
            proto_reply(cmd, seq, PROTO_ST_OK, data, sizeof(data));
            break;
        }

        case PROTO_CMD_GET_SENSOR: {
            const uint8_t data[1] = { settings_get()->sensor_mode };
            proto_reply(cmd, seq, PROTO_ST_OK, data, sizeof(data));
            break;
        }

        case PROTO_CMD_SET_SENSOR: {
            if (plen < 1u) {
                proto_reply(cmd, seq, PROTO_ST_BADLEN, NULL, 0u);
                break;
            }

            if (frame[3] >= (uint8_t)PAW3395_MODE_COUNT) {
                proto_reply(cmd, seq, PROTO_ST_UNSUPPORTED, NULL, 0u);
                break;
            }

            settings_set_mode(frame[3]);

            const uint8_t data[1] = { settings_get()->sensor_mode };
            proto_reply(cmd, seq, PROTO_ST_OK, data, sizeof(data));
            break;
        }

        case PROTO_CMD_GET_LIFT: {
            const uint8_t data[1] = { settings_get()->lift_cut };
            proto_reply(cmd, seq, PROTO_ST_OK, data, sizeof(data));
            break;
        }

        case PROTO_CMD_SET_LIFT: {
            if (plen < 1u) {
                proto_reply(cmd, seq, PROTO_ST_BADLEN, NULL, 0u);
                break;
            }

            if (frame[3] > (uint8_t)PAW3395_LIFT_CUT_2MM) {
                proto_reply(cmd, seq, PROTO_ST_UNSUPPORTED, NULL, 0u);
                break;
            }

            settings_set_lift(frame[3]);

            const uint8_t data[1] = { settings_get()->lift_cut };
            proto_reply(cmd, seq, PROTO_ST_OK, data, sizeof(data));
            break;
        }

        case PROTO_CMD_GET_BATTERY: {
            const uint16_t mv = bat_voltage_mv();

            /* Linear 3.00 V .. 4.20 V; the sensor pin is the coarse gauge here. */
            uint16_t pct = 0u;
            if (mv > 3000u) {
                pct = (uint16_t)((mv - 3000u) / 12u);
            }
            if (pct > 100u) {
                pct = 100u;
            }

            const uint8_t flags = (uint8_t)((bat_charging() ? 0x01u : 0u) |
                                            (bat_power_good() ? 0x02u : 0u) |
                                            (bat_charge_fault() ? 0x04u : 0u));
            const uint8_t data[4] = { (uint8_t)(mv & 0xFFu), (uint8_t)(mv >> 8), (uint8_t)pct, flags };
            proto_reply(cmd, seq, PROTO_ST_OK, data, sizeof(data));
            break;
        }

        case PROTO_CMD_BIO_ACQ: {
            if (plen < 1u) {
                proto_reply(cmd, seq, PROTO_ST_BADLEN, NULL, 0u);
                break;
            }
            settings_set_bio(frame[3]);
            proto_reply(cmd, seq, PROTO_ST_OK, NULL, 0u);
            break;
        }

        case PROTO_CMD_BIO_SLEEP: {
            if (plen < 1u) {
                proto_reply(cmd, seq, PROTO_ST_BADLEN, NULL, 0u);
                break;
            }
            if (frame[3] != 0u) {
                bio_sleep_enable();
            } else {
                bio_sleep_disable();
            }
            proto_reply(cmd, seq, PROTO_ST_OK, NULL, 0u);
            break;
        }

        default:
            proto_reply(cmd, seq, PROTO_ST_UNKNOWN, NULL, 0u);
            break;
    }
}

void proto_handle_get(uint8_t *frame, uint16_t len)
{
    if (frame == NULL) {
        return;
    }

    if (len > sizeof(s_resp)) {
        len = sizeof(s_resp);
    }

    memset(frame, 0, len);
    memcpy(frame, s_resp, len);
}
