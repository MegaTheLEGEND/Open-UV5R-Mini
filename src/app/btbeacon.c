/* Open-UV5R-Mini: BT BEACON (ENABLE_APP_BTBEACON): MENU -> APPS -> BT BEACON. The radio's own BLE beacon, one at a
 * time, non-connectable: its name, an iBeacon (for "find my radio" apps that range iBeacons), or an Eddystone URL.
 * Phones see it in any BLE scanner app. It needs a backend that advertises (bt/bt.h BT_CAP_LE_ADV); the open build
 * has none yet, so it says so. Advertising stops when the app is left.
 *
 * Keys: 1 name, 2 iBeacon, 3 URL, MENU start / stop, UP/DOWN the interval, EXIT back.
 * Its state is in .ram2 (.bss is full). */
#include "app/apps.h"
#if APP_BTBEACON_ENABLE
#include "bt/bt.h"
#include "bt/le_adv.h"
#include "driver/st7735s.h"
#include "ui/helper.h"
#include "ui/status.h"
#include "ui/theme.h"

#define ROW_Y(r) ((uint8_t)(15u + 11u * (r)))

static const uint16_t INTERVALS[] = { 100, 250, 500, 1000, 2000 };
static const char *const MODES[] = { "NAME", "IBEACON", "URL" };
#define BEACON_NAME "UV5R-MINI"
#define BEACON_URL  "https://chirpmyradio.com"
/* this project's own iBeacon UUID (random, made for it): 4f50454e-5556-3552-8d1a-6b2e9a10c7e3 */
static const uint8_t UUID[16] = { 0x4F, 0x50, 0x45, 0x4E, 0x55, 0x56, 0x35, 0x52,
                                  0x8D, 0x1A, 0x6B, 0x2E, 0x9A, 0x10, 0xC7, 0xE3 };

static struct {
	uint8_t data[BT_ADV_MAX];
	uint8_t len;
	uint8_t mode;
	uint8_t ivl;
	bool    on;
	bool    failed;
	bool    redraw;
} S __attribute__((section(".ram2.btbeacon")));

static char *put(char *o, const char *s) { while (*s) *o++ = *s++; *o = 0; return o; }
static void row(uint8_t r, const char *t, uint16_t fg) { UI_PrintPadded(t, 2, ROW_Y(r), fg, COLOR_BG, 21); }

static void build(void)
{
	switch (S.mode) {
	case 0: S.len = LeAdv_BuildName(S.data, BEACON_NAME); break;
	case 1: S.len = LeAdv_BuildIBeacon(S.data, UUID, 1, 1, -59); break;
	default: S.len = LeAdv_BuildEddystoneUrl(S.data, BEACON_URL, -20); break;
	}
}

static void apply(void)
{
	BT_LeAdvStop();
	S.failed = false;
	if (S.on) {
		build();
		if (!S.len || !(BT_Caps() & BT_CAP_LE_ADV) || !BT_LeAdvStart(S.data, S.len, INTERVALS[S.ivl])) {
			S.on = false;
			S.failed = true;
		}
	}
	S.redraw = true;
}

static void draw(bool full)
{
	char b[24], *o;
	static const char H[] = "0123456789ABCDEF";

	if (full) {
		ST7735S_FillRect(0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_BG);
		UI_TitleBar("BT BEACON", "");
	}
	UI_TitleRight(S.on ? "ON AIR" : "OFF", S.on ? COLOR_RX : COLOR_DIM);
	o = put(b, "TYPE ");
	put(o, MODES[S.mode]);
	row(0, b, COLOR_ACCENT);
	row(1, S.mode == 0 ? BEACON_NAME : S.mode == 1 ? "UUID 4F50454E..C7E3" : "CHIRPMYRADIO.COM", COLOR_TEXT);
	o = put(b, "EVERY ");
	o = APPS_PutInt(o, INTERVALS[S.ivl]);
	put(o, " MS");
	row(2, b, COLOR_TEXT);
	build();
	for (uint8_t r = 0, i = 0; r < 3; r++) {   /* the data sent, 10 bytes a row */
		o = b;
		*o = 0;
		for (uint8_t k = 0; k < 10u && i < S.len; k++, i++) {
			*o++ = H[S.data[i] >> 4];
			*o++ = H[S.data[i] & 15];
		}
		*o = 0;
		row((uint8_t)(3 + r), b, COLOR_DIM);
	}
	if (!(BT_Caps() & BT_CAP_LE_ADV)) {
		row(7, "NO BLE ADVERTISING", COLOR_WARN);
		row(8, "IN THIS BUILD", COLOR_WARN);
	} else {
		row(7, S.failed ? "COULD NOT START" : "", COLOR_WARN);
		row(8, "", COLOR_WARN);
	}
	if (full)
		UI_PrintString("1-3 TYPE MENU ON/OFF", 2, 118, COLOR_DIM, COLOR_BG, 1);
}

static void enter(void)
{
	S.mode = 0;
	S.ivl = 2;
	S.on = S.failed = false;
	S.redraw = true;
}

static void leave(void)
{
	BT_LeAdvStop();
	S.on = false;
}

static AppsResult_t key(KEY_Code_t k)
{
	switch (k) {
	case KEY_EXIT:
		return APPS_EXIT;
	case KEY_1: case KEY_2: case KEY_3:
		S.mode = (uint8_t)(k - KEY_1);
		apply();
		break;
	case KEY_MENU:
		S.on = !S.on;
		apply();
		break;
	case KEY_UP:
		if (S.ivl + 1u < sizeof INTERVALS / sizeof INTERVALS[0])
			S.ivl++;
		apply();
		break;
	case KEY_DOWN:
		if (S.ivl)
			S.ivl--;
		apply();
		break;
	default:
		break;
	}
	return APPS_STAY;
}

static AppsResult_t tick(bool full)
{
	if (full || S.redraw)
		draw(full);
	S.redraw = false;
	return APPS_STAY;
}

const AppDef_t APP_BTBEACON = { "BT BEACON", enter, key, tick, leave, false, false,
                                "1 NAME 2 IBEACON\n3 URL\nMENU START/STOP\nUP/DOWN INTERVAL\nEXIT BACK" };
#endif
