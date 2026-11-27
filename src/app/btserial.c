/* Open-UV5R-Mini: BT SERIAL (ENABLE_APP_BTSERIAL): MENU -> APPS -> BT SERIAL, after Flipper Zero's serial-over-
 * Bluetooth: a small terminal on the Bluetooth serial link (SPP in the SDK edition, a BLE data service later). It
 * shows what the phone or PC sends and sends a few lines back. It needs a backend with a serial link (bt/bt.h
 * BT_IsAvailable); the open build has none yet, so it says so.
 *
 * Keys: 1 send "PING", 2 send the selected side's frequency, 0 clear the screen, EXIT back.
 * Its state is in .ram2 (.bss is full). */
#include "app/apps.h"
#if APP_BTSERIAL_ENABLE
#include "bt/bt.h"
#include "driver/st7735s.h"
#include "radio/radio.h"
#include "ui/helper.h"
#include "ui/status.h"
#include "ui/theme.h"

#define ROW_Y(r) ((uint8_t)(13u + 10u * (r)))
#define LINES    9
#define COLS     21

static struct {
	char     line[LINES][COLS + 1];      /* the last lines received, oldest first */
	uint8_t  col;                        /* in the last line */
	uint32_t rx, tx;                     /* bytes */
	bool     was_connected;
	bool     redraw, full;
	char     status[22];
} S __attribute__((section(".ram2.btserial")));

static char *put(char *o, const char *s) { while (*s) *o++ = *s++; *o = 0; return o; }

static void scroll(void)
{
	for (int i = 0; i + 1 < LINES; i++)
		for (int k = 0; k <= COLS; k++)
			S.line[i][k] = S.line[i + 1][k];
	S.line[LINES - 1][0] = 0;
	S.col = 0;
}

static void on_receive(const uint8_t *d, uint16_t n)
{
	S.rx += n;
	for (uint16_t i = 0; i < n; i++) {
		char c = (char)d[i];
		if (c == '\r')
			continue;
		if (c == '\n' || S.col >= COLS) {
			scroll();
			if (c == '\n')
				continue;
		}
		if (c >= 'a' && c <= 'z')
			c = (char)(c - 32);          /* the font has capitals only */
		if (c < 0x20 || c > 0x7E)
			c = '.';
		S.line[LINES - 1][S.col++] = c;
		S.line[LINES - 1][S.col] = 0;
	}
	S.redraw = true;
}

static void send(const char *s)
{
	uint16_t n = 0;
	while (s[n])
		n++;
	if (BT_Send((const uint8_t *)s, n) == 0)
		S.tx += n;
	S.redraw = true;
}

static void draw(bool full)
{
	char *o;

	if (full) {
		ST7735S_FillRect(0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_BG);
		UI_TitleBar("BT SERIAL", "");
	}
	if (!BT_IsAvailable()) {
		UI_TitleRight("NONE", COLOR_WARN);
		UI_PrintPadded("NO BLUETOOTH SERIAL", 2, ROW_Y(0), COLOR_WARN, COLOR_BG, COLS);
		UI_PrintPadded("LINK IN THIS BUILD", 2, ROW_Y(1), COLOR_WARN, COLOR_BG, COLS);
		return;
	}
	UI_TitleRight(BT_IsConnected() ? "LINKED" : "WAITING", BT_IsConnected() ? COLOR_RX : COLOR_DIM);
	for (uint8_t r = 0; r < LINES; r++)
		UI_PrintPadded(S.line[r], 2, ROW_Y(r), COLOR_TEXT, COLOR_BG, COLS);
	o = put(S.status, "RX ");
	o = APPS_PutInt(o, (int)S.rx);
	o = put(o, "  TX ");
	APPS_PutInt(o, (int)S.tx);
	UI_PrintPadded(S.status, 2, 107, COLOR_DIM, COLOR_BG, COLS);
	if (full)
		UI_PrintString("1 PING 2 FREQ 0 CLR", 2, 118, COLOR_DIM, COLOR_BG, 1);
}

static void enter(void)
{
	for (int i = 0; i < LINES; i++)
		S.line[i][0] = 0;
	S.col = 0;
	S.rx = S.tx = 0;
	S.was_connected = false;
	S.redraw = S.full = true;
	BT_Init(on_receive);
}

static void leave(void)
{
	BT_Init(0);                          /* as app.c leaves it */
}

static AppsResult_t key(KEY_Code_t k)
{
	char b[24], *o;

	switch (k) {
	case KEY_EXIT:
		return APPS_EXIT;
	case KEY_1:
		send("PING\r\n");
		break;
	case KEY_2:
		o = put(b, "FREQ ");
		o = APPS_FormatMhz(o, gVfo.freq);
		put(o, " MHZ\r\n");
		send(b);
		break;
	case KEY_0:
		for (int i = 0; i < LINES; i++)
			S.line[i][0] = 0;
		S.col = 0;
		S.redraw = true;
		break;
	default:
		break;
	}
	return APPS_STAY;
}

static AppsResult_t tick(bool full)
{
	bool c = BT_IsConnected();
	if (c != S.was_connected) {
		S.was_connected = c;
		S.redraw = true;
	}
	if (full || S.full)
		draw(true);
	else if (S.redraw)
		draw(false);
	S.redraw = S.full = false;
	return APPS_STAY;
}

const AppDef_t APP_BTSERIAL = { "BT SERIAL", enter, key, tick, leave, false, false,
                                "1 SEND PING\n2 SEND FREQUENCY\n0 CLEAR\nEXIT BACK" };
#endif
