/* Open-UV5R-Mini: bare-metal entry (the fully open build). */
#include "app/app.h"
#include "driver/system.h"

int main(void)
{
	uint32_t last, now;

	SYSTEM_TimeInit();
	APP_Init();
	last = SYSTEM_Ms();
	for (;;) {
		APP_Tick();
		SYSTEM_WaitUntil(last + APP_TICK_MIN_MS);
		now = SYSTEM_Ms();               /* the next tick is told how long this one really was */
		gAppTickMs = (uint16_t)(now - last > 250u ? 250u : now - last);
		last = now;
	}
}
