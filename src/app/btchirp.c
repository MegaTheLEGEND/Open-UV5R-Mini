/* Open-UV5R-Mini: BT CHIRP (ENABLE_APP_BTCHIRP): MENU -> BLUETOOTH -> BT CHIRP. CHIRP programming over Bluetooth:
 * the same protocol as the cable (app/program.c), on the BLE serial link (the Nordic UART Service, bt/gatt.c).
 *
 * While it is open the radio advertises as "UV5R-MINI". On the PC, tools/ble_serial.py connects to it and offers
 * the link as a TCP port; in CHIRP: Radio -> Download (or Upload), port "Custom...", socket://localhost:5005, radio
 * Baofeng UV-5R Mini. Each download or upload is one session; the app stays open for the next one. After an upload
 * the radio loads the new channels and settings at once, as after a cable upload.
 *
 * Keys: 5 pairing mode (app/btpair.h; a passkey shows here), EXIT back (the link closes). State in .ram2 (.bss is full). */
#include "app/apps.h"
#if APP_BTCHIRP_ENABLE
#include "app/btpair.h"
#include "app/program.h"
#include "bt/bt.h"
#include "driver/st7735s.h"
#include "ui/helper.h"
#include "ui/status.h"
#include "ui/theme.h"

#define ROW_Y(r) ((uint8_t)(16u + 11u * (r)))

static struct {
	bool    linked;
	uint8_t reads, writes;
	bool    full;
	char    pair[22];
} S __attribute__((section(".ram2.btchirp")));

static void draw(bool full)
{
#if PROGRAM_BT
	static char b[22];
	char *o;
#endif

	if (full) {
		ST7735S_FillRect(0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_BG);
		UI_TitleBar("BT CHIRP", "");
	}
#if PROGRAM_BT
	UI_TitleRight(S.linked ? "LINKED" : "WAITING", S.linked ? COLOR_RX : COLOR_DIM);
	if (full) {
		UI_PrintString("NAME UV5R-MINI", 2, ROW_Y(0), COLOR_TEXT, COLOR_BG, 1);
		UI_PrintString("PC: BLE_SERIAL.PY", 2, ROW_Y(2), COLOR_DIM, COLOR_BG, 1);
		UI_PrintString("CHIRP PORT CUSTOM:", 2, ROW_Y(3), COLOR_DIM, COLOR_BG, 1);
		UI_PrintString("SOCKET://LOCALHOST", 2, ROW_Y(4), COLOR_DIM, COLOR_BG, 1);
		UI_PrintString(":5005", 2, ROW_Y(5), COLOR_DIM, COLOR_BG, 1);
		UI_PrintString("EXIT CLOSES THE LINK", 2, 118, COLOR_DIM, COLOR_BG, 1);
	}
	o = b;
	*o++ = 'R';
	*o++ = 'E';
	*o++ = 'A';
	*o++ = 'D';
	*o++ = ' ';
	o = APPS_PutInt(o, S.reads);
	for (const char *s = "  WRITTEN "; *s; )
		*o++ = *s++;
	APPS_PutInt(o, S.writes);
	UI_PrintPadded(b, 2, ROW_Y(7), COLOR_TEXT, COLOR_BG, 20);
	UI_PrintPadded(S.pair, 2, ROW_Y(6), S.pair[0] == 'K' ? COLOR_WARN : S.pair[0] == '5' ? COLOR_DIM : COLOR_ACCENT,
	               COLOR_BG, 20);
#else
	if (full) {
		UI_TitleRight("NONE", COLOR_WARN);
		UI_PrintString("NO BLUETOOTH SERIAL", 2, ROW_Y(0), COLOR_WARN, COLOR_BG, 1);
		UI_PrintString("LINK IN THIS BUILD", 2, ROW_Y(1), COLOR_WARN, COLOR_BG, 1);
	}
#endif
}

static void enter(void)
{
	S.linked = false;
	S.reads = S.writes = 0;
	S.full = true;
#if PROGRAM_BT
	BTPAIR_Enter();
	S.pair[0] = 0;
	BT_Init(PROGRAM_BtReceive);
#endif
}

static void leave(void)
{
#if PROGRAM_BT
	BTPAIR_Leave();
	BT_Init(0);
#endif
}

static AppsResult_t key(KEY_Code_t k)
{
#if PROGRAM_BT
	if (BTPAIR_Key(k)) {
		bool big;
		BTPAIR_Tick(S.pair, &big);
		draw(false);
		return APPS_STAY;
	}
#endif
	return k == KEY_EXIT ? APPS_EXIT : APPS_STAY;
}

static AppsResult_t tick(bool full)
{
	AppsResult_t r = APPS_STAY;
#if PROGRAM_BT
	int p = PROGRAM_PollBt();            /* a whole session runs inside this call, with its own screen */
	if (p != PROGRAM_IDLE) {
		if (p == PROGRAM_DONE_WRITTEN) {
			S.writes++;
			r = APPS_RELOAD;             /* app.c loads the new channels and settings */
		} else
			S.reads++;
		S.full = true;
	}
	{
		bool big;
		if (BTPAIR_Tick(S.pair, &big) && !S.full)
			draw(false);
	}
	if (BT_IsConnected() != S.linked) {
		S.linked = !S.linked;
		if (!S.full)
			draw(false);
	}
#endif
	if (full || S.full) {
		draw(true);
		S.full = false;
	}
	return r;
}

const AppDef_t APP_BTCHIRP = { "BT CHIRP", enter, key, tick, leave, false, false,
                               "CHIRP OVER BLUETOOTH\nPC: BLE_SERIAL.PY\nPORT SOCKET://\nLOCALHOST:5005\n5 PAIRING MODE\nEXIT BACK" };
#endif
