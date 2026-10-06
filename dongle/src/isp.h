#ifndef __DONGLE_ISP_H__
#define __DONGLE_ISP_H__

/* On-demand entry to the ROM ISP bootloader for the dongle, so reflashing no
 * longer needs the board to be dug out of its case for a power cycle. The
 * dongle has no BOOT button, so the trigger arrives over USB; see isp.c for how
 * the ROM is made to stay in ISP and what that costs. */
void isp_init(void);
void isp_poll(void);

/* Flag an ISP request from the USB layer (proto_handle_set). Runs in the USB
 * ISR, so it only sets a flag; the erase and reset happen in isp_poll(). */
void isp_request(void);

#endif /* __DONGLE_ISP_H__ */
