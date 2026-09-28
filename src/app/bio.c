/*
 * src/app/bio.c - BIO module bring-up and control.
 *
 * Reset is a plain GPIO (docs/io.csv BIO_RST), held low to keep the module in
 * reset. The acquisition, sleep and checkup modes are single command bytes on
 * the UART3 link the module shares with its USB CDC bridge; the checkup
 * duration is the one 3-byte command.
 */

#include "bio.h"

#include "CH58x_common.h"

/* The real-time packet is exactly 88 bytes on the wire; fail the build if the
 * struct ever drifts from the spec. */
typedef char bio_rt_pack_size_check[(sizeof(bio_rt_pack_t) == 88u) ? 1 : -1];

#define BIO_RST_PIN   GPIO_Pin_11

/* Mode command bytes on the UART3 link. */
#define BIO_CMD_MEASURE_ON    0x8Au
#define BIO_CMD_MEASURE_OFF   0x88u
#define BIO_CMD_SLEEP_ON      0x98u
#define BIO_CMD_SLEEP_OFF     0x00u
#define BIO_CMD_CHECKUP_ON    0x8Eu
#define BIO_CMD_CHECKUP_OFF   0x8Cu
#define BIO_CMD_CHECKUP_TIME  0x84u

void bio_init(void)
{
    GPIOA_ModeCfg(BIO_RST_PIN, GPIO_ModeOut_PP_5mA);
}

void bio_reset(void)
{
    GPIOA_ResetBits(BIO_RST_PIN);   /* assert reset */
    mDelaymS(20);
    GPIOA_SetBits(BIO_RST_PIN);     /* release reset */
    mDelaymS(100);                  /* module boot time */
}

/* UART3_SendString waits for FIFO space, so the byte is not dropped while the
 * CDC bridge is streaming. */
static void bio_send(uint8_t cmd)
{
    UART3_SendString(&cmd, 1u);
}

void bio_measure_enable(void)
{
    bio_send(BIO_CMD_MEASURE_ON);
}

void bio_measure_disable(void)
{
    bio_send(BIO_CMD_MEASURE_OFF);
}

void bio_sleep_enable(void)
{
    bio_send(BIO_CMD_SLEEP_ON);
}

void bio_sleep_disable(void)
{
    bio_send(BIO_CMD_SLEEP_OFF);
}

void bio_checkup_enable(void)
{
    bio_send(BIO_CMD_CHECKUP_ON);
}

void bio_checkup_disable(void)
{
    bio_send(BIO_CMD_CHECKUP_OFF);
}

void bio_set_checkup_time(uint16_t tst)
{
    /* 3-byte frame: 0x84, then TST bits 12..7, then TST bits 6..0. */
    uint8_t cmd[3];

    cmd[0] = BIO_CMD_CHECKUP_TIME;
    cmd[1] = (uint8_t)((tst >> 7) & 0x3Fu);
    cmd[2] = (uint8_t)(tst & 0x7Fu);
    UART3_SendString(cmd, sizeof(cmd));
}

void bio_set_checkup_seconds(uint32_t seconds)
{
    /* time = TST * 8 * 1.28 s = TST * 10.24 s  ->  TST = seconds * 100 / 1024,
     * rounded to the nearest step. */
    bio_set_checkup_time((uint16_t)((seconds * 100u + 512u) / 1024u));
}

/* --- Real-time packet parsing ------------------------------------------- */

#define BIO_RT_HEADER   0xFFu

/* A packet is assembled in the work buffer and published by a single struct
 * copy, so a reader never sees a half-updated packet. */
static bio_rt_pack_t     s_rt_work;
static bio_rt_pack_t     s_rt_latest;
static volatile bool     s_rt_valid;
static volatile uint32_t s_rt_seq;
static uint8_t           s_rt_len;

void bio_rt_feed(uint8_t byte)
{
    if (s_rt_len == 0u) {
        if (byte != BIO_RT_HEADER) {
            return;   /* resynchronise on the header */
        }
    }

    ((uint8_t *)&s_rt_work)[s_rt_len++] = byte;

    if (s_rt_len >= sizeof(s_rt_work)) {
        s_rt_len    = 0u;
        s_rt_latest = s_rt_work;
        s_rt_valid  = true;
        s_rt_seq++;
    }
}

bool bio_rt_read(bio_rt_pack_t *out)
{
    bool ok = false;

    if (out == NULL) {
        return false;
    }

    PFIC_DisableAllIRQ();
    if (s_rt_valid) {
        *out = s_rt_latest;
        ok = true;
    }
    PFIC_EnableAllIRQ();

    return ok;
}

uint32_t bio_rt_seq(void)
{
    return s_rt_seq;
}

bool bio_rt_pack_valid(const bio_rt_pack_t *packet)
{
    if (packet == NULL) {
        return false;
    }

    /* With nothing on the sensor the module sends every metric as zero (and a
     * flat waveform), so any real value marks the packet as usable. */
    return (packet->heartrate != 0u) || (packet->spo2 != 0u) || (packet->bk != 0u);
}
