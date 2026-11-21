/* Open-UV5R-Mini: delays and the watchdog. */
#include <stdint.h>
#include "bsp/ac695n.h"
#include "driver/system.h"

/* Measured on a radio (tests/radio/blink.c, 2026-11-21): about 0.75 loop turns per microsecond after the
 * bootloader, so 1 keeps every delay a little long, never short. */
#define LOOPS_PER_US 1u

/* The watchdog runs after the bootloader: on a radio (2026-11-21) our app restarted at the same point every time
 * (heartbeat ~68) without clears. Clear it as the stock app does, p33_or(0x80, 0x40), through our own P33 access
 * (the stock routine at 0x02000156 is RAM code we don't have). */
void SYSTEM_WatchdogClear(void)
{
	P33_Or(0x80, 0x40);
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
