/* Open-UV5R-Mini: Bluetooth interface.
 *
 * The open build links bt/bt_none.c (no Bluetooth). The optional Bluetooth edition links
 * platform/jl_sdk/bt_spp.c, which needs JieLi's closed btstack/btctrler libraries from the user's own
 * SDK copy (see docs/BLUETOOTH.md). Nothing else in the firmware depends on which one is linked.
 */
#ifndef BT_BT_H
#define BT_BT_H

#include <stdint.h>
/* Inside the JieLi SDK, typedef.h already defines bool (as u8), true and false. */
#ifndef OPEN_UV5R_SDK_TYPES
#include <stdbool.h>
#endif

typedef void (*BT_ReceiveHandler_t)(const uint8_t *data, uint16_t len);

bool BT_IsAvailable(void);   /* false in the open build */
void BT_Init(BT_ReceiveHandler_t on_receive);
bool BT_IsConnected(void);
int  BT_Send(const uint8_t *data, uint16_t len); /* 0 on success */

#endif
