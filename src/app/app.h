/* Open-UV5R-Mini: application entry points, shared by the bare-metal build and the JieLi-SDK task. */
#ifndef APP_APP_H
#define APP_APP_H

/* Time since the last APP_Tick, in ms (main.c sets it each tick; 10 until then and in the Bluetooth edition). The
 * main loop runs as often as every APP_TICK_MIN_MS so the apps feel quick (Sean, 2026-11-27); every timer adds
 * APP_TICK_MS, so it keeps real time however long a tick takes. */
#include <stdint.h>
extern uint16_t gAppTickMs;
#define APP_TICK_MS     gAppTickMs
#define APP_TICK_MIN_MS 2u

void APP_Init(void);
void APP_Tick(void); /* call every APP_TICK_MS */

#endif
