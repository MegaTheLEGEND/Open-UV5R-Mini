/* PC preview of the screens: src/ui/screen_main.c and screen_menu.c drawn into a framebuffer through a stand-in LCD, for a few
 * made-up scenes. tests/ui_preview.py builds it, checks the drawing stays on screen, and writes PNGs. */
#include <stdio.h>
#include <string.h>
#include "driver/st7735s.h"
#include "ui/screen_main.h"
#include "ui/screen_menu.h"
#include "ui/theme.h"

bool test_pins[128];
static uint16_t fb[LCD_HEIGHT][LCD_WIDTH];
static uint8_t wx, wy, ww, wh;
static unsigned wpos, bad;

void ST7735S_SetWindow(uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
	if (!w || !h || x + w > LCD_WIDTH || y + h > LCD_HEIGHT) {
		bad++;
		printf("off screen: window %u,%u %ux%u\n", x, y, w, h);
	}
	wx = x; wy = y; ww = w; wh = h; wpos = 0;
}

void ST7735S_PushPixel(uint16_t c)
{
	unsigned x = wx + wpos % ww, y = wy + wpos / ww;
	if (wpos >= (unsigned)ww * wh || x >= LCD_WIDTH || y >= LCD_HEIGHT)
		bad++;
	else
		fb[y][x] = c;
	wpos++;
}

void ST7735S_End(void)
{
	if (wpos != (unsigned)ww * wh) {
		bad++;
		printf("window %u,%u %ux%u ended after %u pixels\n", wx, wy, ww, wh, wpos);
	}
}

void ST7735S_FillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t c)
{
	ST7735S_SetWindow(x, y, w, h);
	for (unsigned n = (unsigned)w * h; n; n--)
		ST7735S_PushPixel(c);
}

static void dump(const char *path)
{
	FILE *f = fopen(path, "wb");
	fprintf(f, "P6\n%d %d\n255\n", LCD_WIDTH, LCD_HEIGHT);
	for (int y = 0; y < LCD_HEIGHT; y++)
		for (int x = 0; x < LCD_WIDTH; x++) {
			uint16_t c = fb[y][x];
			unsigned char px[3] = { (unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2),
			                        (unsigned char)((c & 31) << 3) };
			fwrite(px, 1, 3, f);
		}
	fclose(f);
}

static void side(UiSide_t *s, uint32_t f, const char *label, int tone_kind, const char *tone)
{
	memset(s, 0, sizeof *s);
	s->freq = f;
	strcpy(s->label, label);
	s->tone_kind = (uint8_t)tone_kind;
	strcpy(s->tone, tone);
	s->meter = -1;
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : ".";
	char path[512];
	UiMain_t v;

	memset(&v, 0, sizeof v);
	side(&v.side[0], 14652000u, "", UI_TONE_NONE, "");
	side(&v.side[1], 44600625u, "CH-003 PMR3", UI_TONE_CTCSS, "88.5");
	v.side[0].high_power = true;
	v.side[1].narrow = true;
	v.side[0].meter = 1;
	v.squelch = 3; v.battery = 3;
	v.bottom[0] = 0;
	v.bottom_color = COLOR_DIM;
	UI_MainDraw(&v, true);
	snprintf(path, sizeof path, "%s/main_idle.ppm", dir);
	dump(path);

	/* receiving on A, dual watch and lamp on, then B in channel mode with DCS and a shift */
	v.side[0].state = UI_STATE_RX;
	v.side[0].meter = 6;
	v.dual_watch = v.lamp = v.locked = v.scan = v.warning = true;
	strcpy(v.battery_text, "100%");               /* MENU -> BATTERY: PERCENT; every status icon on */
	side(&v.side[1], 43950000u, "012 439.500", UI_TONE_DCS, "D023N");
	v.side[1].shift = -1;
	strcpy(v.side[1].big, "CITY RPTR 70");   /* too long for large: two small lines */
	strcpy(v.bottom, "DTMF 123*#");
	v.bottom_color = COLOR_WARN;
	UI_MainDraw(&v, false);
	snprintf(path, sizeof path, "%s/main_rx.ppm", dir);
	dump(path);

	/* B active, typing a frequency, scanning, low battery, airband AM on A */
	side(&v.side[0], 11850000u, "", UI_TONE_NONE, "");
	v.side[0].am = v.side[0].narrow = true;
	v.active = 1;
	strcpy(v.entry, "145.5--  ");
	v.scan = true;
	v.battery = 1;
	v.battery_low = true;
	v.warning = true;
	strcpy(v.bottom, "CHANNEL SAVED");
	v.bottom_color = COLOR_ACCENT;
	UI_MainDraw(&v, false);
	snprintf(path, sizeof path, "%s/main_entry.ppm", dir);
	dump(path);

	/* the same scene drawn from scratch must look the same as the one drawn by changes */
	{
		static uint16_t keep[LCD_HEIGHT][LCD_WIDTH];
		memcpy(keep, fb, sizeof fb);
		memset(fb, 0xAA, sizeof fb);
		UI_MainDraw(&v, true);
		if (memcmp(keep, fb, sizeof fb)) {
			printf("FAIL: drawing only the changes left a different screen than a full redraw\n");
			return 1;
		}
	}
	/* the menu (src/ui/screen_menu.c): the first page (sections with their number ranges, ABOUT last), a section,
	 * a channel name being edited, and SYSTEM with the hidden items after a start with = held. Names and numbers
	 * as the app has them (ENABLE_KEYTEST on). */
	{
		static const char *const PAGE[] = { "CHANNEL", "SCAN", "TRANSMIT", "SYSTEM", "ABOUT" };
		static const char *const PAGE_R[] = { "1-14", "15-21", "22-32", "33-42", 0 };
		static const char *const CH[] = { "SHIFT", "OFFSET", "TX POWER", "BANDWIDTH", "MODULATION", "STEP",
		                                  "SQUELCH" };
		static const char *const SYS[] = { "AUTO LOCK", "MENU EXIT", "= PRESS", "= DOUBLE", "= HOLD", "KEY TEST",
		                                   "REGION", "TX LOCK" };
		UiMenu_t m;
		memset(&m, 0, sizeof m);
		m.title = "MENU";
		m.count = 5;
		m.sel = 0;
		m.cursor = -1;
		for (int r = 0; r < 5; r++) {
			m.name[r] = PAGE[r];
			m.right[r] = PAGE_R[r];
		}
		m.value = "14 ITEMS";
		UI_MenuDraw(&m, true);
		snprintf(path, sizeof path, "%s/menu.ppm", dir);
		dump(path);

		memset(&m, 0, sizeof m);
		m.title = "CHANNEL";
		m.count = 14;
		m.sel = 10;                                 /* SQUELCH, item 11 */
		m.top = UI_MenuTop(m.sel);
		m.cursor = -1;
		for (int r = 0; r < UI_MENU_ROWS; r++) {
			m.name[r] = CH[r];
			m.num[r] = (uint8_t)(m.top + r + 1);
		}
		strcpy(m.corner, "11/42");
		m.value = "3";
		UI_MenuDraw(&m, true);
		snprintf(path, sizeof path, "%s/menu_channel.ppm", dir);
		dump(path);

		m.sel = 1;                                  /* CH NAME, item 2, being edited */
		m.top = 0;
		{
			static const char *const TOP[] = { "FREQUENCY", "CH NAME", "RX TONE", "TX TONE", "SHIFT", "OFFSET",
			                                   "TX POWER" };
			for (int r = 0; r < UI_MENU_ROWS; r++) {
				m.name[r] = TOP[r];
				m.num[r] = (uint8_t)(r + 1);
			}
		}
		strcpy(m.corner, "02/42");
		m.value = "CITY RPTR   ";
		m.edit = true;
		m.cursor = 5;
		UI_MenuDraw(&m, true);
		snprintf(path, sizeof path, "%s/menu_name.ppm", dir);
		dump(path);

		memset(&m, 0, sizeof m);
		m.title = "SYSTEM";
		m.count = 12;
		m.sel = 10;                                 /* REGION, item 43 */
		m.top = UI_MenuTop(m.sel);
		m.cursor = -1;
		for (int r = 0; r < UI_MENU_ROWS; r++) {
			m.name[r] = SYS[m.top + r - 4];
			m.num[r] = (uint8_t)(33 + m.top + r);
			m.hidden[r] = m.top + r >= 10;
		}
		strcpy(m.corner, "43/44");
		m.corner_warn = true;
		m.value = "US";
		m.edit = true;
		UI_MenuDraw(&m, true);
		snprintf(path, sizeof path, "%s/menu_hidden.ppm", dir);
		dump(path);
	}

	if (bad) {
		printf("FAIL: %u pixels or windows off the screen\n", bad);
		return 1;
	}
	printf("ui_preview: all passed\n");
	return 0;
}
