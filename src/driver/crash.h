/* Open-UV5R-Mini: what happens when the firmware crashes (release 1.0.0).
 *
 * The bootloader leaves the watchdog running and the main loop clears it every tick (driver/system.h), so a hang
 * ends in a watchdog restart. This adds:
 *   - handlers for the core's exceptions (vectors 0-2) from power-on: they note the vector and address, then stop
 *     clearing the watchdog, so the radio restarts cleanly (before, the vectors were set only when the FM radio
 *     opened; the FM receiver still puts its own in while it runs, which do the same);
 *   - a record in RAM that start-up does not clear (.ram2): "running" is set at start and cleared at a deliberate
 *     power-off, so finding it set at start means the last run ended in a restart, not the power switch;
 *   - a safe start: after CRASH_SAFE_AFTER restarts in a row that each came within CRASH_STABLE_MS of power-on,
 *     the things that start by themselves (broadcast FM in the VFO, the power-on picture, the beeps' DAC) are left
 *     off for that run, so a crash at start-up can't become a restart loop.
 * The update mode (PTT-A + = at power-on) is checked before any of this, so it stays reachable either way.
 * A power-off that cuts the supply clears the RAM (a magic word and its complement guard the record). */
#ifndef DRIVER_CRASH_H
#define DRIVER_CRASH_H

#include <stdbool.h>
#include <stdint.h>

#define CRASH_STABLE_MS  20000u      /* running this long counts as a good start */
#define CRASH_SAFE_AFTER 2u          /* restarts in a row before that: the next start is a safe one */

void CRASH_Init(void);               /* at start-up, after the update-mode check */
void CRASH_Tick(uint32_t uptime_ms); /* every tick */
void CRASH_CleanOff(void);           /* before a deliberate power-off or restart */
bool CRASH_Restarted(void);          /* the last run ended in a restart (exception or watchdog) */
bool CRASH_Safe(void);               /* this is a safe start */
/* "CRASH E1 01E0ABCD" (an exception: its vector and address) or "RESTARTED"; 0 when the last run ended normally */
const char *CRASH_Text(void);

#endif
