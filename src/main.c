/* Open-UV5R-Mini: bare-metal entry (the fully open build). */
#include "app/app.h"
#include "driver/system.h"

int main(void)
{
	APP_Init();
	for (;;) {
		APP_Tick();
		SYSTEM_DelayMs(APP_TICK_MS);
	}
}
