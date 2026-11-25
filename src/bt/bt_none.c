/* Open-UV5R-Mini: Bluetooth stubs for the fully open build. */
#include "bt/bt.h"
#include "driver/system.h"

bool BT_IsAvailable(void) { return false; }
void BT_Init(BT_ReceiveHandler_t on_receive) { (void)on_receive; }
bool BT_IsConnected(void) { return false; }
int  BT_Send(const uint8_t *data, uint16_t len) { (void)data; (void)len; return -1; }
void BT_WaitMs(uint32_t ms) { SYSTEM_DelayMs(ms); }
