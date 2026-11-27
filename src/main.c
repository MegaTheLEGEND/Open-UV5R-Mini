/* Open-UV5R-Mini: bare-metal entry (the fully open build). */
#include "app/app.h"
#include "driver/system.h"

int main(void)
{
	uint32_t next;

	SYSTEM_TimeInit();
	APP_Init();
	next = SYSTEM_Ms();
	for (;;) {
		APP_Tick();
		next += APP_TICK_MS;             /* one tick each 10 ms of real time */
		if ((int32_t)(SYSTEM_Ms() - next) > 100)
			next = SYSTEM_Ms();          /* a long tick: don't race to catch up */
		SYSTEM_WaitUntil(next);
	}
}
