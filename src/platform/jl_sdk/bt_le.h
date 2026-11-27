/* Open-UV5R-Mini: the bare-metal BLE backend (bt_le.c, ENABLE_BT_LE). Apps use bt/bt.h; app.c calls BTLE_Tick. */
#ifndef PLATFORM_JL_SDK_BT_LE_H
#define PLATFORM_JL_SDK_BT_LE_H

#include <stdbool.h>
#include <stdint.h>

/* every main-loop tick: runs the controller (its messages, threads and timers) while Bluetooth is on */
void     BTLE_Tick(void);
bool     BTLE_IsOn(void);
uint32_t BTLE_Reports(void);

/* for the cable monitor and BT INFO: how far the start got and what came back */
typedef struct {
	uint8_t  step;                       /* 0 off, 1-6 start steps (6 = running), 20 stopping */
	uint8_t  last_event, last_sub;       /* the last HCI event code and LE subevent */
	uint32_t starts, events, msgs, msgs_other;
	uint32_t conns, acl_in, acl_out, acl_dropped;   /* the serial link */
} BtLeStats_t;
extern BtLeStats_t gBtLe;

#endif
