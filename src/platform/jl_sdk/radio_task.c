/* Open-UV5R-Mini: runs the radio application as a task of the JieLi SDK's OS (Bluetooth edition only).
 *
 * Call open_uv5r_start() once from the SDK application's init, after the Bluetooth stack is started.
 * See docs/BLUETOOTH.md.
 */
#include "typedef.h"
#include "os/os_api.h"
#include "app/app.h"

#define RADIO_TASK_PRIO   3
#define RADIO_TASK_STACK  1024 /* words */
#define OS_TICK_MS        (1000 / OS_TICKS_PER_SEC) /* 10 ms: OS_TICKS_PER_SEC is 100 in os_type.h */

static void radio_task(void *arg)
{
	(void)arg;
	APP_Init();
	for (;;) {
		APP_Tick();
		os_time_dly(APP_TICK_MS / OS_TICK_MS);
	}
}

void open_uv5r_start(void)
{
	os_task_create(radio_task, NULL, RADIO_TASK_PRIO, RADIO_TASK_STACK, 0, "open_uv5r");
}
