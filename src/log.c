#include "log.h"
#include "CH58x_common.h"

#include <stdarg.h>
#include <stdio.h>

/* Bytes of buffered output text; must be a power of two. */
#define LOG_TX_BUF_SIZE 1024u

/* Longest single formatted line (prefix + message + '\n'). */
#define LOG_LINE_MAX    160u

static uint8_t      s_level = LOG_LEVEL;
static log_clock_fn s_clock = 0;

/* Single-producer / single-consumer ring: log_printf() owns head, the UART1
 * TX interrupt owns tail. No locking is needed on a single core. */
static volatile uint8_t  s_tx_buf[LOG_TX_BUF_SIZE];
static volatile uint16_t s_tx_head;
static volatile uint16_t s_tx_tail;
static volatile uint32_t s_dropped;

static uint16_t tx_used(void)
{
    return (uint16_t)((s_tx_head - s_tx_tail) & (LOG_TX_BUF_SIZE - 1u));
}

/* Non-blocking: drop the whole line when the ring is full so lines never
 * interleave, and count the loss. */
static void tx_enqueue(const uint8_t *p, uint16_t n)
{
    if ((uint16_t)(LOG_TX_BUF_SIZE - 1u - tx_used()) < n) {
        s_dropped++;
        return;
    }

    for (uint16_t i = 0; i < n; i++) {
        s_tx_buf[s_tx_head] = p[i];
        s_tx_head = (uint16_t)((s_tx_head + 1u) & (LOG_TX_BUF_SIZE - 1u));
    }

    UART1_INTCfg(ENABLE, RB_IER_THR_EMPTY);
}

/* Fired whenever the TX FIFO drains. Re-armed by log_printf() on enqueue and
 * by itself while data remains, so it stays silent (and cheap) when idle. */
__INTERRUPT void UART1_IRQHandler(void)
{
    UART1_INTCfg(DISABLE, RB_IER_THR_EMPTY);
    (void)R8_UART1_IIR;   /* reading IIR clears the pending flag */

    while ((s_tx_tail != s_tx_head) && (R8_UART1_TFC != UART_FIFO_SIZE)) {
        R8_UART1_THR = s_tx_buf[s_tx_tail];
        s_tx_tail = (uint16_t)((s_tx_tail + 1u) & (LOG_TX_BUF_SIZE - 1u));
    }

    if (s_tx_tail != s_tx_head) {
        UART1_INTCfg(ENABLE, RB_IER_THR_EMPTY);
    }
}

void log_init(uint8_t level)
{
    s_level = level;
    s_tx_head = 0;
    s_tx_tail = 0;
    s_dropped = 0;
    UART1_INTCfg(DISABLE, RB_IER_THR_EMPTY);
    PFIC_EnableIRQ(UART1_IRQn);
}

void log_set_level(uint8_t level)
{
    s_level = level;
}

void log_set_clock(log_clock_fn fn)
{
    s_clock = fn;
}

uint32_t log_dropped(void)
{
    return s_dropped;
}

void log_printf(uint8_t level, const char *tag, const char *fmt, ...)
{
    static const char sev[] = { 'E', 'W', 'I', 'D' };
    const char c = (level <= LOG_LVL_DEBUG) ? sev[level] : '?';
    char line[LOG_LINE_MAX];
    int n;

    if (level > s_level) {
        return;
    }

    if (s_clock) {
        n = snprintf(line, sizeof line, "[%lu][%c][%s] ",
                     (unsigned long)s_clock(), c, tag);
    } else {
        n = snprintf(line, sizeof line, "[%c][%s] ", c, tag);
    }
    if (n < 0) {
        return;
    }
    if (n > (int)(sizeof line - 1u)) {
        n = (int)(sizeof line - 1u);
    }

    va_list ap;
    va_start(ap, fmt);
    const int m = vsnprintf(line + n, sizeof line - (size_t)n, fmt, ap);
    va_end(ap);

    if (m > 0) {
        const int room = (int)sizeof line - n - 1;   /* keep room for '\n' */
        n += (m < room) ? m : room;
    }

    line[n++] = '\n';

    tx_enqueue((const uint8_t *)line, (uint16_t)n);
}
