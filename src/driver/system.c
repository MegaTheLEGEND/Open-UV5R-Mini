/* Open-UV5R-Mini: delays, the millisecond clock and the watchdog. */
#include <stdbool.h>
#include <stdint.h>
#include "bsp/ac695n.h"
#include "driver/system.h"

/* Measured on a radio (tests/radio/blink.c, 2026-11-21): about 0.75 loop turns per microsecond after the
 * bootloader, so 1 keeps every delay a little long, never short. Only the microsecond waits use it now: the CPU
 * clock changes (the FM radio runs it at 160 MHz), so the loop runs faster than it was measured at. */
#define LOOPS_PER_US 1u

/* The millisecond clock: Timer 2 (JieLi's JL_TIMER2, 0x1E0600: CON, CNT, PRD) on the crystal clock: CON bit 0 =
 * count, bits 2-3 = 2 the "osc" clock (SDK handshake_timer.c: 24 MHz), bits 4-7 = 3 divide by 64 (SDK irflt.c
 * timer_div), bit 14 = clear pending. Neither the stock app nor our code used Timer 2. 16 bits at 375 kHz wrap every
 * 175 ms, read far more often than that.
 * History (Sean, 2026-11-27): the 10 ms tick as a CPU loop counted uptime about 2x fast; then this timer with the
 * rate seen on Timer 3 while the FM radio ran (2.69 MHz) counted about 9x fast, so outside the FM radio the clock
 * really is 24 MHz. While the FM radio runs, it sets the rate it measures against its own interrupt
 * (SYSTEM_SetTicksPerSec), whatever the FM start does to the clock. */
#define T2_CON REG32(0x001E0600u)
#define T2_CNT REG32(0x001E0604u)
#define T2_PRD REG32(0x001E0608u)
#define T2_TICKS_PER_SEC 375000u         /* 24 MHz / 64 */

static bool     s_timer;                 /* the timer counts: otherwise the loop delays keep the clock */
static uint16_t s_last;
static uint32_t s_frac, s_ms, s_raw, s_tps = T2_TICKS_PER_SEC;

void (*gSystemIdle)(void);

/* The watchdog runs after the bootloader: on a radio (2026-11-21) our app restarted at the same point every time
 * (heartbeat ~68) without clears. Clear it as the stock app does, p33_or(0x80, 0x40), through our own P33 access
 * (the stock routine at 0x02000156 is RAM code we don't have). */
void SYSTEM_WatchdogClear(void)
{
	P33_Or(0x80, 0x40);
}

void SYSTEM_TimeInit(void)
{
	uint16_t a;

	T2_CON = 1u << 14;
	T2_CNT = 0;
	T2_PRD = 0xFFFFu;
	T2_CON = 1u << 0 | 2u << 2 | 3u << 4;
	a = (uint16_t)T2_CNT;
	SYSTEM_DelayUs(2000);                /* hundreds of counts: a timer that doesn't move isn't used */
	s_last = (uint16_t)T2_CNT;
	s_timer = s_last != a;
	s_frac = 0;
}

uint32_t SYSTEM_Ms(void)
{
	if (s_timer) {
		uint16_t n = (uint16_t)T2_CNT, d = (uint16_t)(n - s_last);

		s_last = n;
		s_raw += d;
		s_frac += (uint32_t)d * 1000u;
		while (s_frac >= s_tps) {
			s_frac -= s_tps;
			s_ms++;
		}
	}
	return s_ms;
}

uint32_t SYSTEM_Raw(void)
{
	SYSTEM_Ms();
	return s_raw;
}

uint32_t SYSTEM_TicksPerSec(void)
{
	return s_tps;
}

void SYSTEM_SetTicksPerSec(uint32_t tps)
{
	SYSTEM_Ms();
	s_tps = tps >= 1000u ? tps : T2_TICKS_PER_SEC;
	s_frac = 0;
}

void SYSTEM_DelayUs(uint32_t us)
{
	for (volatile uint32_t i = us * LOOPS_PER_US; i; i--)
		;
}

void SYSTEM_DelayMs(uint32_t ms)
{
	uint32_t t0 = SYSTEM_Ms();

	if (!s_timer) {
		while (ms--) {
			SYSTEM_WatchdogClear();
			SYSTEM_DelayUs(1000);
			s_ms++;
		}
		return;
	}
	while (SYSTEM_Ms() - t0 <= ms) {     /* never short: the first millisecond may be part gone */
		SYSTEM_WatchdogClear();
		SYSTEM_DelayUs(50);
	}
}

void SYSTEM_WaitUntil(uint32_t ms)
{
	while ((int32_t)(SYSTEM_Ms() - ms) < 0) {
		SYSTEM_WatchdogClear();
		if (gSystemIdle)
			gSystemIdle();
		else
			SYSTEM_DelayUs(50);
	}
}
