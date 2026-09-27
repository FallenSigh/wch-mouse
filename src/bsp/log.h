#ifndef __LOG_H__
#define __LOG_H__

#include <stdint.h>

/* Severity levels: a message is emitted when its level is <= the active
 * verbosity, so a higher number means "more verbose". */
#define LOG_LVL_ERROR 0
#define LOG_LVL_WARN  1
#define LOG_LVL_INFO  2
#define LOG_LVL_DEBUG 3

/* Compile-time ceiling. Messages above it are removed by the preprocessor,
 * so their format strings never reach flash. Override with
 *   -DLOG_LEVEL=LOG_LVL_DEBUG   (or a plain number). */
#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LVL_INFO
#endif

/* Optional millisecond clock for line timestamps; pass NULL to omit them. */
typedef uint32_t (*log_clock_fn)(void);

void log_init(uint8_t level);
void log_set_level(uint8_t level);
void log_set_clock(log_clock_fn fn);

/* Number of lines dropped because the TX ring was full. */
uint32_t log_dropped(void);

/* Backend. Prefer the LOG_* macros. Safe only from thread context - it blocks
 * on the UART FIFO, so do not call it from an ISR. */
void log_printf(uint8_t level, const char *tag, const char *fmt, ...);

#if LOG_LEVEL >= LOG_LVL_ERROR
#define LOG_E(tag, ...) log_printf(LOG_LVL_ERROR, (tag), __VA_ARGS__)
#else
#define LOG_E(tag, ...) ((void)0)
#endif

#if LOG_LEVEL >= LOG_LVL_WARN
#define LOG_W(tag, ...) log_printf(LOG_LVL_WARN, (tag), __VA_ARGS__)
#else
#define LOG_W(tag, ...) ((void)0)
#endif

#if LOG_LEVEL >= LOG_LVL_INFO
#define LOG_I(tag, ...) log_printf(LOG_LVL_INFO, (tag), __VA_ARGS__)
#else
#define LOG_I(tag, ...) ((void)0)
#endif

#if LOG_LEVEL >= LOG_LVL_DEBUG
#define LOG_D(tag, ...) log_printf(LOG_LVL_DEBUG, (tag), __VA_ARGS__)
#else
#define LOG_D(tag, ...) ((void)0)
#endif

#endif
