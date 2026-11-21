/* Open-UV5R-Mini: delays and the watchdog. */
#include <stdint.h>
#include "driver/system.h"

/* Measured on a radio (tests/radio/blink.c, 2026-11-21): about 0.75 loop turns per microsecond after the
 * bootloader, so 1 keeps every delay a little long, never short. */
#define LOOPS_PER_US 1u

#define ROM_P33_OR 0x02000156u /* void f(uint16_t reg, uint8_t mask), the stock app's p33_or */

/* Off: on a radio (2026-11-21) every build that wrote P33 register 0x80 cut out within a few seconds, while a
 * test that never touched it ran on, so the watchdog isn't running after the bootloader. Roadmap 2.2. */
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
