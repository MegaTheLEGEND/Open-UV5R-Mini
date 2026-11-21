/* Open-UV5R-Mini: delays and the watchdog. */
#include <stdint.h>
#include "driver/system.h"

#define LOOPS_PER_US 32u /* about 3 cycles per loop at 96 MHz */

#define ROM_P33_OR 0x02000156u /* void f(uint16_t reg, uint8_t mask), the stock app's p33_or */

/* Works once start.S sets the CPU up as stock does (tests/radio/blink2.c, 2026-11-21); before that, this first
 * ROM call restarted the chip. */
void SYSTEM_WatchdogClear(void)
{
	((void (*)(uint16_t, uint8_t))(uintptr_t)ROM_P33_OR)(0x80, 0x40);
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
