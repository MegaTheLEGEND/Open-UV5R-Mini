/* Open-UV5R-Mini: Bluetooth (SPP serial port) for the optional Bluetooth edition: Wireless CPS.
 *
 * Compiled only inside the user's own copy of JieLi's BR23 SDK (fw-AC63_BT_SDK, app spp_and_le), against its
 * spp_user.h and avctp_user.h and its closed btstack.a / btctrler.a. Not part of the open build. See
 * docs/BLUETOOTH.md and tools/sdk/add_radio.sh, which wires this file in.
 *
 * Stock's Wireless CPS is the cable's programming protocol over SPP (docs/BLUETOOTH.md, "Stock's Wireless CPS"):
 * received bytes go to app/program.c's receive side (PROGRAM_BtReceive, given to BT_Init), and program.c's replies
 * come back here (BT_Send) while the session runs on Bluetooth.
 *
 * The SPP service itself (its SDP record, named "JL_SPP" like stock's, and its RFCOMM server) is the SDK library's
 * own, switched on by USER_SUPPORT_PROFILE_SPP. The library's record says RFCOMM channel 1 where stock's says 2;
 * the channel is fixed inside the library, and a PC or phone finds it through SDP, so it is left as it is.
 *
 * The SDK app's own SPP module (apps/spp_and_le/modules/bt/spp_trans.c, a loopback demo) registers its callbacks
 * when the stack is up (transport_spp_init); add_radio.sh makes that function call open_uv5r_spp_attach() last,
 * so ours replace the demo's.
 */
#include <stdint.h>              /* before avctp_user.h, which uses uint8_t without including it */
#include "typedef.h"
#include "spp_user.h"
#include "btstack/avctp_user.h"
#include "os/os_api.h"
#define OPEN_UV5R_SDK_TYPES
#include "bt/bt.h"

#define SPP_SEND_MAX   512u  /* USER_CTRL_SPP_SEND_DATA takes up to 512 bytes */
#define SPP_SEND_BUSY  3u    /* its "the previous packet is not sent yet" return */
#define SEND_TRIES     50    /* 50 x 10 ms */
#define OS_TICK_MS     (1000 / OS_TICKS_PER_SEC) /* 10 ms: OS_TICKS_PER_SEC is 100 in os_type.h */

extern int bt_comm_edr_sniff_clean(void); /* the SDK app's (app_comm_edr.c): leave sniff mode while data flows */

static BT_ReceiveHandler_t receive_handler;
static volatile u8 connected;

static void spp_state(u8 state)
{
	if (state == SPP_USER_ST_CONNECT)
		connected = 1;
	else if (state == SPP_USER_ST_DISCONN)
		connected = 0;
}

/* Called in the Bluetooth stack's task: only hand the bytes on (PROGRAM_BtReceive queues them). */
static void spp_receive(void *priv, u8 *buf, u16 len)
{
	(void)priv;
	bt_comm_edr_sniff_clean();
	if (receive_handler)
		receive_handler(buf, len);
}

/* Register our SPP callbacks with the library (it keeps one of each). Also called by the SDK's transport_spp_init
 * once the stack is up (add_radio.sh), after it has registered the demo's. */
void open_uv5r_spp_attach(void)
{
	struct spp_operation_t *api = NULL;

	spp_get_operation_table(&api);
	if (api) {
		api->regist_recieve_cbk(0, spp_receive);
		api->regist_state_cbk(0, spp_state);
	}
}

bool BT_IsAvailable(void) { return true; }

void BT_Init(BT_ReceiveHandler_t on_receive)
{
	receive_handler = on_receive;
	open_uv5r_spp_attach();
}

bool BT_IsConnected(void) { return connected; }

/* The public SDK's send call: returns 0 once the data is taken (it waits until sent), 3 while the previous packet is
 * still going out. Stock does the same through its own SDK's command 34 (sub_01E3DECE). */
int BT_Send(const uint8_t *data, uint16_t len)
{
	if (!connected || !len || len > SPP_SEND_MAX)
		return -1;
	for (int i = 0; i < SEND_TRIES; i++) {
		u32 r = user_send_cmd_prepare(USER_CTRL_SPP_SEND_DATA, len, (u8 *)data);
		if (r == 0)
			return 0;
		if (r != SPP_SEND_BUSY)
			return -1;
		os_time_dly(1);
	}
	return -1;
}

void BT_WaitMs(uint32_t ms)
{
	os_time_dly((ms + OS_TICK_MS - 1u) / OS_TICK_MS); /* lets the Bluetooth tasks run, unlike a spin */
}

/* The rest of bt/bt.h: this backend is SPP only so far. */
uint8_t     BT_Caps(void) { return BT_CAP_SPP; }
const char *BT_Backend(void) { return "JL SDK"; }
bool        BT_GetAddress(uint8_t addr[6]) { (void)addr; return false; }
bool        BT_LeScanStart(bool active) { (void)active; return false; }
void        BT_LeScanStop(void) {}
bool        BT_LePoll(BT_LeReport_t *out) { (void)out; return false; }
bool        BT_LeAdvStart(const uint8_t *data, uint8_t len, uint16_t interval_ms)
{
	(void)data; (void)len; (void)interval_ms;
	return false;
}
void        BT_LeAdvStop(void) {}

void           BT_SetPairable(bool on) { (void)on; }
bool           BT_Pairable(void) { return false; }
BT_PairState_t BT_PairState(void) { return BT_PAIR_NONE; }
int32_t        BT_Passkey(void) { return -1; }
