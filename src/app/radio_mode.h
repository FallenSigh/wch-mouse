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
    RADIO_MODE_RF = 0, /* preferred: 2.4G link to the dongle */
    RADIO_MODE_BLE = 1
} radio_mode_t;

void radio_mode_init(void);
radio_mode_t radio_mode_get(void);
bool radio_mode_is(radio_mode_t mode);

/* True once the switch combo has been held long enough that releasing it will
 * switch modes. Lets the UI preview the pending choice while it is held. */
bool radio_mode_armed(void);

/* Driven from the scan path with the debounced button state: side 1 and the
 * wheel click held together ask for the other radio mode and releasing them
 * performs the switch. The BOOT strap is no longer involved, which also removes
 * the old footgun - a strap still held low when this reset lands would have
 * entered the ISP bootloader instead of the application. */
void radio_mode_poll(uint32_t now_ms, bool combo_held);

#endif /* __RADIO_MODE_H__ */
