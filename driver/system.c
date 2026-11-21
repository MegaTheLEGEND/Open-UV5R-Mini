/* Open-UV5R-Mini: delays and the watchdog. */
#include <stdint.h>
#include "driver/system.h"

#define LOOPS_PER_US 32u /* about 3 cycles per loop at 96 MHz */

#define ROM_P33_OR 0x02000156u /* void f(uint16_t reg, uint8_t mask), the stock app's p33_or */

/* Off for now. On a radio (2026-11-21, tests/radio/blink.c) the first call to the ROM's p33_or(0x80, 0x40) was
 * followed by a restart, and the app ran over 10 s without it and without a reset, so the watchdog doesn't seem to
 * be running at this point. To revisit with the watchdog enable (roadmap 2.2). */
void SYSTEM_WatchdogClear(void)
{
#ifdef SYSTEM_WDT_ROM_CLEAR
	((void (*)(uint16_t, uint8_t))(uintptr_t)ROM_P33_OR)(0x80, 0x40);
#endif
}

void SYSTEM_DelayUs(uint32_t us)
{
	for (volatile uint32_t i = us * LOOPS_PER_US; i; i--)
		;
}

void SYSTEM_DelayMs(uint32_t ms)
{
	while (ms--) {
		SYSTEM_WatchdogClear();
		SYSTEM_DelayUs(1000);
	}
}
