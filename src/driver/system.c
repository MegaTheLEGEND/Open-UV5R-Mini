/* Open-UV5R-Mini: delays, the millisecond clock and the watchdog. */
#include <stdbool.h>
#include <stdint.h>
#include "bsp/ac695n.h"
#include "driver/system.h"

/* Measured on a radio (tests/radio/blink.c, 2026-11-21): about 0.75 loop turns per microsecond after the
 * bootloader, so 1 keeps every delay a little long, never short. Only the microsecond waits use it now: the CPU
 * clock changes (the FM radio runs it at 160 MHz), so the loop runs faster than it was measured at. */
#define LOOPS_PER_US 1u

/* The millisecond clock: Timer 2 (JieLi's JL_TIMER2, 0x1E0600: CON, CNT, PRD) counting the crystal, set up as the
 * FM code's Timer 3 (src/platform/jl_sdk/fmrx.c): CON bit 0 = count, bit 3 = crystal (SDK handshake_timer.c), bit
 * 14 = clear pending. Neither the stock app nor our code uses Timer 2. Wraps every 65536 counts (about 24 ms), so it
 * is read at least that often: every main loop tick and every millisecond of a delay. Before 2026-11-27 the 10 ms
 * tick was a CPU loop and Sean saw the uptime count at about twice real time. */
#define T2_CON REG32(0x001E0600u)
#define T2_CNT REG32(0x001E0604u)
#define T2_PRD REG32(0x001E0608u)
#define T2_TICKS_PER_MS 2686u            /* Timer 3 measured on Sean's radio against the PC clock (2026-11-26) */

static bool     s_timer;                 /* the timer counts: otherwise the loop delays keep the clock */
static uint16_t s_last;
static uint32_t s_frac, s_ms;

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
	T2_CON = 1u << 0 | 1u << 3;
	a = (uint16_t)T2_CNT;
	SYSTEM_DelayUs(200);                 /* about 500 counts: a timer that doesn't move isn't used */
	s_last = (uint16_t)T2_CNT;
	s_timer = s_last != a;
	s_frac = 0;
}

uint32_t SYSTEM_Ms(void)
{
	if (s_timer) {
		uint16_t n = (uint16_t)T2_CNT;

		s_frac += (uint16_t)(n - s_last);
		s_last = n;
		while (s_frac >= T2_TICKS_PER_MS) {
			s_frac -= T2_TICKS_PER_MS;
			s_ms++;
		}
	}
	return s_ms;
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
		SYSTEM_DelayUs(50);
	}
}
