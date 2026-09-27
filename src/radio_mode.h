#ifndef __RADIO_MODE_H__
#define __RADIO_MODE_H__

#include <stdbool.h>
#include <stdint.h>

/* Which radio transport this boot runs. The CH585 has a single radio, so the
 * mouse is either the 2.4G RF device (dongle link, lower latency) or a BLE
 * peripheral - never both at once, and never switched live. The choice is
 * user selectable, kept in DataFlash and applied at boot; USB is independent
 * of it and still takes priority whenever a host is attached. */
typedef enum {
    RADIO_MODE_RF  = 0,   /* preferred: 2.4G link to the dongle */
    RADIO_MODE_BLE = 1
} radio_mode_t;

void         radio_mode_init(void);
radio_mode_t radio_mode_get(void);
bool         radio_mode_is(radio_mode_t mode);
void         radio_mode_poll(uint32_t now_ms);

#endif /* __RADIO_MODE_H__ */
