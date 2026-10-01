#ifndef __ISP_H__
#define __ISP_H__

#include <stdint.h>

/* On-demand entry to the ROM ISP bootloader, so reflashing no longer needs a
 * power cycle with the BOOT strap held. See isp.c for how the ROM is made to
 * stay in ISP and what that costs. */
void isp_init(void);
void isp_poll(uint32_t now_ms);

#endif /* __ISP_H__ */
