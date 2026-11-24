/* Open-UV5R-Mini: the apps' screens. See ui/screen_apps.h. */
#include "ui/screen_apps.h"
#include "driver/st7735s.h"
#include "ui/helper.h"
#include "ui/theme.h"

static unsigned text_len(const char *s)
{
	unsigned n = 0;
	while (s && s[n])
		n++;
	return n;
}

static void title_bar(const char *title, const char *right)
{
	ST7735S_FillRect(0, 0, LCD_WIDTH, 11, COLOR_BAR);
	UI_PrintString(title, 2, 2, COLOR_TEXT, COLOR_BAR, 1);
	if (right)
		UI_PrintString(right, (uint8_t)(126 - 6 * text_len(right)), 2, COLOR_DIM, COLOR_BAR, 1);
}

/* ------------------------------------------------------------------------------------------- spectrum */
void UI_SpectrumFrame(const char *step, const char *left, const char *centre, const char *right)
{
	ST7735S_FillRect(0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_BG);
	title_bar("SPECTRUM", step);
	ST7735S_FillRect(0, (uint8_t)(UI_SPECTRUM_TOP + UI_SPECTRUM_H), LCD_WIDTH, 1, COLOR_DIM);
	UI_PrintString(left, 0, 99, COLOR_DIM, COLOR_BG, 1);
	UI_PrintString(centre, (uint8_t)(64 - 3 * text_len(centre)), 99, COLOR_TEXT, COLOR_BG, 1);
	UI_PrintString(right, (uint8_t)(128 - 6 * text_len(right)), 99, COLOR_DIM, COLOR_BG, 1);
	for (uint8_t i = 0; i < UI_SPECTRUM_N; i++)
		UI_SpectrumBar(i, 0, false);
}

void UI_SpectrumPeak(const char *text, bool warn)
{
	UI_PrintPadded(text, 0, 113, warn ? COLOR_WARN : COLOR_INFO, COLOR_BG, 21);
}

void UI_SpectrumBar(uint8_t i, uint8_t h, bool peak)
{
	uint8_t x = (uint8_t)(i * (LCD_WIDTH / UI_SPECTRUM_N)), w = LCD_WIDTH / UI_SPECTRUM_N;

	if (i >= UI_SPECTRUM_N)
		return;
	if (h > UI_SPECTRUM_H)
		h = UI_SPECTRUM_H;
	if (h < UI_SPECTRUM_H)
		ST7735S_FillRect(x, UI_SPECTRUM_TOP, w, (uint8_t)(UI_SPECTRUM_H - h), COLOR_BG);
	if (i == UI_SPECTRUM_N / 2 && h < UI_SPECTRUM_H - 4)   /* the centre, where the VFO is: a tick at the top */
		ST7735S_FillRect(x, UI_SPECTRUM_TOP, w, 4, COLOR_DIM);
	if (h)
		ST7735S_FillRect(x, (uint8_t)(UI_SPECTRUM_TOP + UI_SPECTRUM_H - h), w, h, peak ? COLOR_WARN : COLOR_RX);
}

/* ------------------------------------------------------------------------------------------- signal meter */
static UiMeter_t s_meter_shown;

static uint8_t meter_x(int16_t dbm)                /* -130..-40 dBm across 4..124 */
{
	int v = dbm < -130 ? -130 : dbm > -40 ? -40 : dbm;
	return (uint8_t)(4 + (v + 130) * 120 / 90);
}

void UI_MeterDraw(const UiMeter_t *m, bool full)
{
	const UiMeter_t *o = &s_meter_shown;
	char b[8];

	if (full) {
		ST7735S_FillRect(0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_BG);
		title_bar("SIGNAL", m->freq);
		UI_PrintString("DBM", 104, 20, COLOR_DIM, COLOR_BG, 1);
		ST7735S_FillRect(0, 59, LCD_WIDTH, 1, COLOR_BAR);
		ST7735S_FillRect(0, 112, LCD_WIDTH, 1, COLOR_BAR);
	}
	if (full || m->dbm != o->dbm || m->open != o->open) {
		/* the level, large: "-87" (scale 3, 18 px a character) */
		char *p = b;
		int v = m->dbm;
		if (v < 0) {
			*p++ = '-';
			v = -v;
		}
		if (v >= 100)
			*p++ = (char)('0' + v / 100);
		*p++ = (char)('0' + v / 10 % 10);
		*p++ = (char)('0' + v % 10);
		*p = 0;
		ST7735S_FillRect(4, 16, 96, 24, COLOR_BG);
		UI_PrintString(b, 4, 16, m->open ? COLOR_RX : COLOR_TEXT, COLOR_BG, 3);
	}
	if (full || m->sunits[0] != o->sunits[0] || m->sunits[1] != o->sunits[1] || m->sunits[3] != o->sunits[3]
	    || m->sunits[4] != o->sunits[4])
		UI_PrintPadded(m->sunits, 98, 32, COLOR_INFO, COLOR_BG, 5);
	if (full || m->dbm != o->dbm || m->peak_dbm != o->peak_dbm) {
		uint8_t x = meter_x(m->dbm), pk = meter_x(m->peak_dbm);
		ST7735S_FillRect(4, 48, 120, 8, COLOR_FAINT);
		ST7735S_FillRect(4, 48, (uint8_t)(x - 4), 8, m->dbm >= -93 ? COLOR_WARN : COLOR_RX);   /* S9 and up: yellow */
		ST7735S_FillRect(pk > 5 ? (uint8_t)(pk - 1) : 4, 46, 2, 12, COLOR_TX);
	}
	/* history: redrawn whole when it changes (124 columns of at most 50 rows) */
	if (full || m->hist_n != o->hist_n || m->hist[UI_METER_HIST - 1] != o->hist[UI_METER_HIST - 1]
	    || m->hist[0] != o->hist[0]) {
		for (int i = 0; i < UI_METER_HIST; i++) {
			uint8_t h = i < m->hist_n ? m->hist[i] : 0, x = (uint8_t)(2 + i);
			if (h > 50)
				h = 50;
			if (h < 50)
				ST7735S_FillRect(x, 61, 1, (uint8_t)(50 - h), COLOR_BG);
			if (h)
				ST7735S_FillRect(x, (uint8_t)(111 - h), 1, h, COLOR_ACCENT);
		}
	}
	if (full || m->peak_dbm != o->peak_dbm || m->tick_sound != o->tick_sound) {
		char t[22], *p = t;
		const char *q = "PEAK ";
		int v = m->peak_dbm < 0 ? -m->peak_dbm : m->peak_dbm;
		while (*q)
			*p++ = *q++;
		if (m->peak_dbm < 0)
			*p++ = '-';
		if (v >= 100)
			*p++ = (char)('0' + v / 100);
		*p++ = (char)('0' + v / 10 % 10);
		*p++ = (char)('0' + v % 10);
		q = m->tick_sound ? "  TICK ON" : "  TICK OFF";
		while (*q)
			*p++ = *q++;
		*p = 0;
		UI_PrintPadded(t, 2, 116, COLOR_DIM, COLOR_BG, 21);
	}
	s_meter_shown = *m;
}

/* ------------------------------------------------------------------------------------------- info page */
void UI_InfoDraw(const UiInfo_t *p, bool full)
{
	if (full) {
		ST7735S_FillRect(0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_BG);
		title_bar(p->title, 0);
	}
	for (int r = 0; r < UI_INFO_ROWS; r++) {
		uint8_t y = (uint8_t)(15 + 11 * r);
		if (!p->label[r])
			continue;
		if (full)
			UI_PrintString(p->label[r], 2, y, COLOR_DIM, COLOR_BG, 1);
		UI_PrintPadded(p->value[r] ? p->value[r] : "", 56, y, COLOR_TEXT, COLOR_BG, 12);
	}
	if (full && p->hint)
		UI_PrintString(p->hint, 2, 118, COLOR_DIM, COLOR_BG, 1);
}

/* ------------------------------------------------------------------------------------------- games */
void UI_GameFrame(const char *title)
{
	ST7735S_FillRect(0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_BG);
	title_bar(title, 0);
	/* the board: UI_GAME_W x UI_GAME_H cells from (1, 12), with a frame round it */
	ST7735S_FillRect(0, 11, LCD_WIDTH, 1, COLOR_DIM);
	ST7735S_FillRect(0, (uint8_t)(12 + UI_GAME_H * UI_GAME_CELL), LCD_WIDTH, 1, COLOR_DIM);
}

void UI_GameCell(uint8_t x, uint8_t y, uint16_t color)
{
	if (x >= UI_GAME_W || y >= UI_GAME_H)
		return;
	ST7735S_FillRect((uint8_t)(1 + x * UI_GAME_CELL), (uint8_t)(12 + y * UI_GAME_CELL), UI_GAME_CELL - 1,
	                 UI_GAME_CELL - 1, color);
}

void UI_GameScore(const char *text)
{
	ST7735S_FillRect(60, 0, 68, 11, COLOR_BAR);
	UI_PrintString(text, (uint8_t)(126 - 6 * text_len(text)), 2, COLOR_WARN, COLOR_BAR, 1);
}
