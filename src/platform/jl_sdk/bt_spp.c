/* Open-UV5R-Mini: Bluetooth (SPP serial port) for the optional Bluetooth edition.
 *
 * Compiled only inside the user's own JieLi AC695N (BR23) SDK tree, against its spp_user.h and its closed
 * btstack.a / btctrler.a. Not part of the open build. See docs/BLUETOOTH.md.
 *
 * Uses only the SDK's public SPP interface (spp_get_operation_table and the callbacks in spp_user.h).
 */
#include "typedef.h"
#include "spp_user.h"
#define OPEN_UV5R_SDK_TYPES
#include "bt/bt.h"

static struct spp_operation_t *spp_api;
static BT_ReceiveHandler_t receive_handler;
static volatile bool connected;

static void spp_state(u8 state)
{
	if (state == SPP_USER_ST_CONNECT)
		connected = true;
	else if (state == SPP_USER_ST_DISCONN)
		connected = false;
}

static void spp_receive(void *priv, u8 *buf, u16 len)
{
	(void)priv;
	if (receive_handler)
		receive_handler(buf, len);
}

bool BT_IsAvailable(void) { return true; }

void BT_Init(BT_ReceiveHandler_t on_receive)
{
	receive_handler = on_receive;
	spp_get_operation_table(&spp_api);
	if (spp_api) {
		spp_api->regist_recieve_cbk(0, spp_receive);
		spp_api->regist_state_cbk(0, spp_state);
	}
}

bool BT_IsConnected(void) { return connected; }

int BT_Send(const uint8_t *data, uint16_t len)
{
	if (!spp_api || !connected)
		return -1;
	return spp_api->send_data(0, (void *)data, len) == SPP_USER_ERR_NONE ? 0 : -1;
}
