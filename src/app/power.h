#ifndef __POWER_H__
#define __POWER_H__

#include <stdbool.h>
#include <stdint.h>

/* Standby: after POWER_IDLE_MS with no motion, buttons or wheel - and only
 * while running on battery - the firmware parks the peripherals and puts the
 * MCU into its low-power sleep.
 *
 * The PAW3395 keeps working on its own: it downshifts Run -> Rest1 -> Rest2 ->
 * Rest3 when idle and still signals motion on its MOTION pin, which is wired to
 * PA7 and armed as the GPIO wake source. A charger or cable insert wakes
 * through BAT, so the board can never be stranded asleep.
 *
 * The caller must re-arm anything derived from the core clock when
 * power_poll() reports that it slept. */
void power_init(void);

/* Defer standby: call on any input activity (motion, button, wheel). */
void power_note_activity(uint32_t now_ms);

/* Run from the main loop. Returns true when a standby cycle completed. */
bool power_poll(uint32_t now_ms);

#endif /* __POWER_H__ */
