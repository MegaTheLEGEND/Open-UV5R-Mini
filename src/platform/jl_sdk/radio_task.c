/* Open-UV5R-Mini: runs the radio application as a task of the JieLi SDK's OS (Bluetooth edition only).
 *
 * open_uv5r_start() is called once from the SDK app's start, right after btstack_init() (tools/sdk/add_radio.sh).
 * See docs/BLUETOOTH.md.
 *
 * Priority 1, the lowest the SDK's own tasks use (app_core): the Bluetooth tasks (btctrler 4, btstack 3) must be able
 * to run while our code waits, and parts of it still wait by spinning (SYSTEM_DelayMs, UART_Receive), as on bare
 * metal. A higher-priority task preempts it at the next OS tick.
 */
#include "typedef.h"
#include "os/os_api.h"
#include "app/app.h"

#define RADIO_TASK_PRIO   1
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
