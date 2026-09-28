#ifndef __PAW3395_PORT_H__
#define __PAW3395_PORT_H__

#include "paw3395.h"

#ifdef __cplusplus
extern "C" {
#endif

bool paw3395_port_init(struct paw3395_dev *dev);

/* True while the sensor's MOTION pin reports a fresh sample. The pin is wired
 * to PB17 and, as a flying wire, to PA7; this reads the PA7 copy. Active low
 * (asserted while motion data is ready), and cleared by the burst read. */
bool paw3395_motion_ready(void);

/* IRQ-driven motion. While started, a falling edge on MOTION (PA7, active low,
 * read via SPI0 DMA so the scan never blocks) accumulates a raw delta pair that
 * paw3395_motion_read() drains. Stop before any blocking sensor configuration
 * (init/cpi/mode/lift) and start again afterwards. */
void paw3395_motion_start(void);
void paw3395_motion_stop(void);

/* Drain the accumulated raw delta since the last call, topping the DMA pipeline
 * up if the pin is still asserted. Safe to call every scan with no motion. */
void paw3395_motion_read(int16_t *dx, int16_t *dy);

#ifdef __cplusplus
}
#endif

#endif /* __PAW3395_PORT_H__ */
