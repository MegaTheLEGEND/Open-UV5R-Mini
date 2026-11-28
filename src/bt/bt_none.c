/* Open-UV5R-Mini: Bluetooth stubs for the fully open build (no Bluetooth; bt/bt.h). */
#include "bt/bt.h"
#include "driver/system.h"

uint8_t     BT_Caps(void) { return 0; }
const char *BT_Backend(void) { return "NONE"; }
bool        BT_GetAddress(uint8_t addr[6]) { (void)addr; return false; }

bool BT_IsAvailable(void) { return false; }
void BT_Init(BT_ReceiveHandler_t on_receive) { (void)on_receive; }
bool BT_IsConnected(void) { return false; }
int  BT_Send(const uint8_t *data, uint16_t len) { (void)data; (void)len; return -1; }

bool BT_LeScanStart(bool active) { (void)active; return false; }
void BT_LeScanStop(void) {}
bool BT_LePoll(BT_LeReport_t *out) { (void)out; return false; }

bool BT_LeAdvStart(const uint8_t *data, uint8_t len, uint16_t interval_ms)
{
	(void)data; (void)len; (void)interval_ms;
	return false;
}
void BT_LeAdvStop(void) {}

void           BT_SetPairable(bool on) { (void)on; }
bool           BT_Pairable(void) { return false; }
BT_PairState_t BT_PairState(void) { return BT_PAIR_NONE; }
int32_t        BT_Passkey(void) { return -1; }
void BT_WaitMs(uint32_t ms) { SYSTEM_DelayMs(ms); }
