/* Open-UV5R-Mini: BT INFO (ENABLE_APP_BTINFO), after Flipper Zero's Bluetooth settings page: MENU -> APPS -> BT INFO.
 * What the linked Bluetooth backend can do (bt/bt.h BT_Caps), whether a link is up, the radio's own address if the
 * backend knows it, and the first bytes of the stock firmware's Bluetooth info area (BTIF, flash 0xFE000 on NRF),
 * read only, raw, as stored. Refreshed every second; any key goes back.
 * Its state is in .ram2 (the stock app's RAM, free under our firmware): .bss is full. */
#include "app/apps.h"
#if APP_BTINFO_ENABLE
#include "bt/bt.h"
#include "driver/flash.h"
#if BT_LE_ENABLE
#include "platform/jl_sdk/bt_le.h"
#endif
#include "ui/screen_apps.h"

#define BTIF_ADDR 0xFE000u               /* NRF (docs/FIRMWARE.md); this firmware is built for NRF only */

static struct {
	UiInfo_t p;
	char     caps[13], scan[13], link[13], addr[13], btif0[13], btif1[13], stack[13];
	uint16_t ms;
} S __attribute__((section(".ram2.btinfo")));

static char *put(char *o, const char *s) { while (*s) *o++ = *s++; *o = 0; return o; }

static char *hex(char *o, const uint8_t *b, unsigned n)
{
	static const char H[] = "0123456789ABCDEF";
	for (unsigned i = 0; i < n; i++) {
		*o++ = H[b[i] >> 4];
		*o++ = H[b[i] & 15];
	}
	*o = 0;
	return o;
}

static void fill(void)
{
	uint8_t c = BT_Caps(), a[6];
	char *o;

	o = S.caps;
	*o = 0;
	if (!c)
		put(o, "NONE");
	if (c & BT_CAP_SPP)
		o = put(o, "SPP ");
	if (c & BT_CAP_LE_DATA)
		o = put(o, "BLE ");
	if (c & BT_CAP_HID)
		o = put(o, "HID");
	o = S.scan;
	*o = 0;
	if (c & BT_CAP_LE_SCAN)
		o = put(o, "SCAN ");
	if (c & BT_CAP_LE_ADV)
		o = put(o, "ADVERTISE");
	if (!(c & (BT_CAP_LE_SCAN | BT_CAP_LE_ADV)))
		put(o, "-");
	put(S.link, !BT_IsAvailable() ? "-" : BT_IsConnected() ? "CONNECTED" : "WAITING");
#if BT_LE_ENABLE
	/* the BLE backend: OFF, how far its start got (1-5) if it stopped there, or ON and the events it has sent */
	if (!gBtLe.step)
		put(S.stack, "OFF");
	else if (gBtLe.step == 6) {
		o = put(S.stack, "ON EV ");
		APPS_PutInt(o, (int)(gBtLe.events % 100000u));
	} else {
		o = put(S.stack, "STEP ");
		APPS_PutInt(o, gBtLe.step);
	}
#else
	put(S.stack, "NOT BUILT");
#endif
	if (BT_GetAddress(a))
		hex(S.addr, a, 6);
	else
		put(S.addr, "UNKNOWN");
}

static void enter(void)
{
	uint8_t b[10];
	uint32_t e = FLASH_Read(BTIF_ADDR, b, sizeof b);

	if (e == FLASH_OK) {
		hex(S.btif0, b, 6);
		hex(S.btif1, b + 6, 4);
	} else {                             /* driver/flash.h: E1 continuous-read mode, E2 time-out */
		b[0] = (uint8_t)(e >> 24);
		hex(put(S.btif0, "READ ERR "), b, 1);
		S.btif1[0] = 0;
	}
	for (int r = 0; r < UI_INFO_ROWS; r++)
		S.p.label[r] = S.p.value[r] = 0;
	S.p.title = "BT INFO";
	S.p.label[0] = "BACKEND";  S.p.value[0] = BT_Backend();
	S.p.label[1] = "LINKS";    S.p.value[1] = S.caps;
	S.p.label[2] = "BLE";      S.p.value[2] = S.scan;
	S.p.label[3] = "LINK";     S.p.value[3] = S.link;
	S.p.label[4] = "ADDRESS";  S.p.value[4] = S.addr;
	S.p.label[5] = "BTIF 0-5"; S.p.value[5] = S.btif0;
	S.p.label[6] = "BTIF 6-9"; S.p.value[6] = S.btif1;
	S.p.label[7] = "CONTROL";  S.p.value[7] = S.stack;
	S.p.hint = "ANY KEY: BACK";
	S.ms = 0;
	fill();
}

static void leave(void) {}

static AppsResult_t key(KEY_Code_t k)
{
	(void)k;
	return APPS_EXIT;
}

static AppsResult_t tick(bool full)
{
	if ((S.ms = (uint16_t)(S.ms + 10u)) >= 1000u) {
		S.ms = 0;
		fill();
		UI_InfoDraw(&S.p, full);
	} else if (full)
		UI_InfoDraw(&S.p, true);
	return APPS_STAY;
}

const AppDef_t APP_BTINFO = { "BT INFO", enter, key, tick, leave, false, false, "ANY KEY LEAVE" };
#endif
