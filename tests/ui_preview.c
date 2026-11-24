/* PC preview of the main screen: src/ui/screen_main.c drawn into a framebuffer through a stand-in LCD, for a few
 * made-up scenes. tests/ui_preview.py builds it, checks the drawing stays on screen, and writes PNGs. */
#include <stdio.h>
#include <string.h>
#include "driver/st7735s.h"
#include "ui/screen_main.h"
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
	v.volume = 10; v.squelch = 3; v.battery = 3;
	strcpy(v.bottom, "STEP 12.5K");
	v.bottom_color = COLOR_DIM;
	UI_MainDraw(&v, true);
	snprintf(path, sizeof path, "%s/main_idle.ppm", dir);
	dump(path);

	/* receiving on A, dual watch and lamp on, then B in channel mode with DCS and a shift */
	v.side[0].state = UI_STATE_RX;
	v.side[0].meter = 6;
	v.dual_watch = v.lamp = v.locked = true;
	side(&v.side[1], 43950000u, "CH-012 RPTR", UI_TONE_DCS, "D023N");
	v.side[1].shift = -1;
	strcpy(v.side[1].big, "RPTR");
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
	strcpy(v.bottom, "MENU *BW #LAMP");
	v.bottom_color = COLOR_DIM;
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
	if (bad) {
		printf("FAIL: %u pixels or windows off the screen\n", bad);
		return 1;
	}
	printf("ui_preview: all passed\n");
	return 0;
}
