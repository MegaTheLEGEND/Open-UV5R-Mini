/* Open-UV5R-Mini: delays, the millisecond clock and the watchdog. */
#ifndef DRIVER_SYSTEM_H
#define DRIVER_SYSTEM_H

#include <stdint.h>

/* The millisecond clock (crystal Timer 2, driver/system.c). SYSTEM_TimeInit once at start-up; until then, or if the
 * timer doesn't count, the clock moves only with SYSTEM_DelayMs. SYSTEM_Ms must be read at least every 24 ms. */
void     SYSTEM_TimeInit(void);
uint32_t SYSTEM_Ms(void);
void     SYSTEM_WaitUntil(uint32_t ms);  /* until SYSTEM_Ms() reaches ms, keeping the watchdog fed */
uint32_t SYSTEM_Raw(void);               /* the timer's own counts so far (24 MHz / 64 outside the FM radio) */
void     SYSTEM_SetTicksPerSec(uint32_t tps);   /* the timer's real rate; 0 = the crystal's */
uint32_t SYSTEM_TicksPerSec(void);       /* SYSTEM_Raw counts a second */
/* Called over and over while the main loop waits for its next tick (SYSTEM_WaitUntil), when set: work that wants
 * to run more often than every 10 ms (the FM radio's RDS decoding). Not called from SYSTEM_DelayMs. */
extern void (*gSystemIdle)(void);

/* SYSTEM_DelayUs is a CPU loop (long at the boot clock, shorter at the FM clock); SYSTEM_DelayMs counts the timer
 * and clears the watchdog as it waits. */
void SYSTEM_DelayUs(uint32_t us);
void SYSTEM_DelayMs(uint32_t ms);

/* Clear the watchdog as the stock app does: p33_or(0x80, 0x40) through the mask ROM (docs/EMULATOR.md).
 * Harmless if the watchdog isn't running. */
void SYSTEM_WatchdogClear(void);

#endif
