#ifndef __BIO_H__
#define __BIO_H__

#include <stdbool.h>
#include <stdint.h>

/* BIO module: reset line plus the mode commands on the UART3 link.
 *
 * UART3 is also the module's USB CDC bridge, so its port must already be
 * initialised (main.c does that) before any mode command is sent.
 */

/* Configure the reset GPIO. Call before bio_reset(). */
void bio_init(void);

/* Pulse the reset line (active low) and wait for the module to boot. */
void bio_reset(void);

/* Mode commands, one byte each on the UART3 link:
 *   acquisition mode (采集模式)  0x8A on / 0x88 off
 *   sleep (休眠)                 0x98 on / 0x00 off
 *   checkup mode (体检模式)      0x8E on / 0x8C off */
void bio_measure_enable(void);
void bio_measure_disable(void);
void bio_sleep_enable(void);
void bio_sleep_disable(void);

/* Time the module needs after a sleep-off before it will accept another
 * command; sending one straight after the wake byte loses it. */
#define BIO_WAKE_DELAY_MS 200u
void bio_checkup_enable(void);
void bio_checkup_disable(void);

/* Checkup duration - the only 3-byte command (first byte 0x84). TST is a
 * 14-bit count and the real time is TST * 8 * 1.28 s (= TST * 10.24 s); a TST
 * of 0 keeps the checkup running until it is stopped. Send it after
 * bio_measure_enable() and before bio_checkup_enable(). */
void bio_set_checkup_time(uint16_t tst);

/* Same, taking the duration in seconds (rounded to the nearest TST step). */
void bio_set_checkup_seconds(uint32_t seconds);

/* Latest real-time packet: 88 bytes, sent once per 64 samples (~1.28 s) while
 * acquisition is on. The 0xFF header alone delimits a packet (no other 0xFF
 * appears in one), and the fields follow the JFC103 spec: acdata[] is the
 * heart-rate waveform (-128..127), then heartrate, spo2, bk, rsv[8], sdnn,
 * rmssd, nn50, pnn50, rra[6], rsv2[2]. */
typedef struct __attribute__((packed)) {
    uint8_t header;    /* 0xFF */
    int8_t acdata[64]; /* waveform, -128..127 */
    uint8_t heartrate;
    uint8_t spo2;
    uint8_t bk;
    uint8_t rsv[8]; /* [0] fatigue, [3] systolic, [4] diastolic, ... */
    uint8_t sdnn;
    uint8_t rmssd;
    uint8_t nn50;
    uint8_t pnn50;
    uint8_t rra[6];
    uint8_t rsv2[2];
} bio_rt_pack_t;

/* Feed one received UART3 byte into the parser. Called from the UART3 ISR. */
void bio_rt_feed(uint8_t byte);

/* Copy the latest complete packet; false until the first one has arrived. */
bool bio_rt_read(bio_rt_pack_t *out);

/* Counts complete packets, so a consumer can refresh only on new data. */
uint32_t bio_rt_seq(void);

/* True when the packet carries a real measurement, i.e. not the all-zero
 * "no contact" packet the module sends with nothing on the sensor. */
bool bio_rt_pack_valid(const bio_rt_pack_t *packet);

#endif /* __BIO_H__ */
