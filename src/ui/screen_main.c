/* Open-UV5R-Mini: the main screen (layout in ui/screen_main.h). */
#include "ui/screen_main.h"
#include "driver/st7735s.h"
#include "ui/helper.h"
#include "ui/icons.h"
#include "ui/theme.h"

#define BAR_H     11u
#define PANEL_Y0  13u
#define PANEL_H   48u
#define PANEL_GAP 3u
#define BOTTOM_Y  116u

static UiMain_t s_shown;

/* the firmware has no C library: memcpy and memset only (driver/mem.c) */
static int differ(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a != *b;
}

char *UI_FormatFreq(char *b, uint32_t f)
{
	uint32_t mhz = f / 100000u, rest = f % 100000u;
	char *p = b;

	*p++ = mhz >= 100 ? (char)('0' + mhz / 100 % 10) : ' ';
	*p++ = (char)('0' + mhz / 10 % 10);
	*p++ = (char)('0' + mhz % 10);
	*p++ = '.';
	for (uint32_t d = 10000u; d; d /= 10)
		*p++ = (char)('0' + rest / d % 10);
	*p = 0;
	return b;
}

/* ------------------------------------------------------------------------------------------- status bar */
static void flag_icon(const Icon_t *icon, uint8_t x, bool on, uint16_t color)
{
	if (on)
		UI_DrawIcon(icon, x, 1, color, COLOR_BAR);
	else
		ST7735S_FillRect(x, 1, icon->w, icon->h, COLOR_BAR);
}

static void draw_battery(const UiMain_t *v)
{
	const uint8_t x = 109;
	uint16_t c = v->battery_low ? COLOR_TX : COLOR_RX;

	if (v->battery_hide) {
		ST7735S_FillRect(x, 1, ICON_STATUS_BATTERY.w, ICON_STATUS_BATTERY.h, COLOR_BAR);
		return;
	}
	UI_DrawIcon(&ICON_STATUS_BATTERY, x, 1, v->battery_low ? COLOR_TX : COLOR_DIM, COLOR_BAR);
	for (int i = 0; i < 4; i++)       /* four cells of 2 x 5 inside the frame */
		ST7735S_FillRect((uint8_t)(x + 2 + 3 * i), 3, 2, 5, i < v->battery ? c : COLOR_BAR);
}

static void draw_status(const UiMain_t *v, bool full)
{
	const UiMain_t *o = &s_shown;
	char b[4];

	if (full) {
		ST7735S_FillRect(0, 0, LCD_WIDTH, BAR_H, COLOR_BAR);
		UI_DrawIcon(&ICON_STATUS_SQUELCH, 2, 1, COLOR_DIM, COLOR_BAR);   /* no volume: the radio has a knob */
	}
	if (full || v->squelch != o->squelch)
		UI_PrintString(UI_FormatDec(b, v->squelch, 1), 17, 2, COLOR_TEXT, COLOR_BAR, 1);
	if (full || v->dual_watch != o->dual_watch)
		flag_icon(&ICON_STATUS_DUALWATCH, 51, v->dual_watch, COLOR_INFO);
	if (full || v->scan != o->scan || v->scan_hold != o->scan_hold)
		flag_icon(&ICON_STATUS_SCAN, 68, v->scan, v->scan_hold ? COLOR_RX : COLOR_WARN);
	if (full || v->lamp != o->lamp || v->strobe != o->strobe)
		flag_icon(&ICON_STATUS_LAMP, 80, v->lamp, v->strobe ? COLOR_TX : COLOR_WARN);
	if (full || v->warning != o->warning)
		flag_icon(&ICON_STATUS_WARNING, 92, v->warning, COLOR_TX);
	if (full || v->locked != o->locked)
		flag_icon(&ICON_STATUS_LOCK, 102, v->locked, COLOR_WARN);
	if (full || v->battery != o->battery || v->battery_low != o->battery_low || v->battery_hide != o->battery_hide)
		draw_battery(v);
}

/* ------------------------------------------------------------------------------------------- side panels */
static uint8_t panel_y(uint8_t side) { return (uint8_t)(PANEL_Y0 + side * (PANEL_H + PANEL_GAP)); }

static void draw_meter(uint8_t y, int8_t lit, uint8_t state)
{
	if (lit < 0) {
		ST7735S_FillRect(104, y, 22, 9, COLOR_BG);
		return;
	}
	for (int i = 0; i < UI_METER_BARS; i++) {
		uint8_t h = (uint8_t)(3 + i), x = (uint8_t)(105 + 3 * i);
		uint16_t c = i < lit ? (state == UI_STATE_TX ? COLOR_TX : COLOR_RX) : COLOR_FAINT;
		if (h < 9)
			ST7735S_FillRect(x, y, 2, (uint8_t)(9 - h), COLOR_BG);
		ST7735S_FillRect(x, (uint8_t)(y + 9 - h), 2, h, c);
	}
}

/* transmit power: three rising bars (3, 5, 7 pixels), one lit for low, all three for high */
static void draw_power(uint8_t x, uint8_t y, bool high, bool active)
{
	for (int i = 0; i < 3; i++) {
		uint8_t h = (uint8_t)(3 + 2 * i), bx = (uint8_t)(x + 3 * i);
		ST7735S_FillRect(bx, y, 2, (uint8_t)(9 - h), COLOR_BG);
		ST7735S_FillRect(bx, (uint8_t)(y + 9 - h), 2, h, i == 0 || high ? (active ? COLOR_WARN : COLOR_DIM) : COLOR_FAINT);
	}
}

static uint16_t freq_color(const UiSide_t *s, bool active)
{
	if (s->state == UI_STATE_TX)
		return COLOR_TX;
	if (s->state == UI_STATE_RX)
		return COLOR_RX;
	return active ? COLOR_TEXT : COLOR_DIM;
}

static void draw_tags(const UiSide_t *s, uint8_t y, bool active)
{
	uint16_t c = active ? COLOR_INFO : COLOR_DIM;
	uint8_t x = 2;

	ST7735S_FillRect(0, y, LCD_WIDTH, 9, COLOR_BG);
	UI_DrawIcon(s->am ? &ICON_MOD_AM : &ICON_MOD_FM, x, y, c, COLOR_BG);
	x += 15;
	UI_DrawIcon(s->narrow ? &ICON_BW_NARROW : &ICON_BW_WIDE, x, y, c, COLOR_BG);
	x += 10;
	draw_power(x, y, s->high_power, active);
	x += 11;
	if (s->shift) {
		UI_DrawIcon(s->shift > 0 ? &ICON_SHIFT_PLUS : &ICON_SHIFT_MINUS, x, y, c, COLOR_BG);
		x += 9;
	}
	if (s->tone_kind != UI_TONE_NONE) {
		const Icon_t *t = s->tone_kind == UI_TONE_DCS ? &ICON_TONE_DCS : &ICON_TONE_CTCSS;
		UI_DrawIcon(t, x, y, c, COLOR_BG);
		UI_PrintString(s->tone, (uint8_t)(x + t->w + 2), (uint8_t)(y + 1), active ? COLOR_TEXT : COLOR_DIM, COLOR_BG, 1);
	}
}

static void draw_side(const UiMain_t *v, uint8_t n, bool full)
{
	const UiSide_t *s = &v->side[n], *o = &s_shown.side[n];
	bool active = v->active == n, was = s_shown.active == n;
	bool typing = active && v->entry[0], typed = was && s_shown.entry[0];
	uint8_t y = panel_y(n);
	char b[12];

	if (full || active != was) {
		UI_DrawIcon(n ? &ICON_SIDE_B : &ICON_SIDE_A, 2, (uint8_t)(y + 1), active ? COLOR_ACCENT : COLOR_DIM, COLOR_BG);
		ST7735S_FillRect(0, (uint8_t)(y + PANEL_H - 1), LCD_WIDTH, 1, active ? COLOR_ACCENT : COLOR_BAR);
	}
	if (full || active != was || differ(s->label, o->label))
		UI_PrintPadded(s->label[0] ? s->label : "VFO", 14, (uint8_t)(y + 2), active ? COLOR_TEXT : COLOR_DIM, COLOR_BG, 12);
	if (full || s->state != o->state) {
		if (s->state == UI_STATE_IDLE)
			ST7735S_FillRect(92, (uint8_t)(y + 1), 9, 9, COLOR_BG);
		else
			UI_DrawIcon(s->state == UI_STATE_TX ? &ICON_STATE_TX : &ICON_STATE_RX, 92, (uint8_t)(y + 1),
			            s->state == UI_STATE_TX ? COLOR_TX : COLOR_RX, COLOR_BG);
	}
	if (full || s->meter != o->meter || s->state != o->state)
		draw_meter((uint8_t)(y + 1), s->meter, s->state);
	if (typing) {
		if (full || !typed || differ(v->entry, s_shown.entry))
			UI_PrintString(v->entry, 10, (uint8_t)(y + 14), COLOR_WARN, COLOR_BG, 2);
	} else if (full || typed || active != was || s->freq != o->freq || differ(s->big, o->big) || s->state != o->state) {
		const char *t = s->big[0] ? s->big : UI_FormatFreq(b, s->freq);
		unsigned len = 0;
		while (t[len])
			len++;
		if (len <= 9) {                         /* large, 9 characters */
			char p[10];
			unsigned i = 0;
			for (; i < 9 && t[i]; i++)
				p[i] = t[i];
			for (; i < 9; i++)
				p[i] = ' ';
			p[9] = 0;
			UI_PrintString(p, 10, (uint8_t)(y + 14), freq_color(s, active), COLOR_BG, 2);
		} else {
			/* too long for large: two lines of small text, split at the space nearest the middle (dropped), or
			 * half way through a name with no space */
			char l1[13], l2[13];
			unsigned cut = (len + 1) / 2, skip = 0, i;
			for (unsigned d = 0; d <= len / 2; d++) {
				if (cut >= d && cut - d > 0 && t[cut - d] == ' ') { cut -= d; skip = 1; break; }
				if (cut + d < len && t[cut + d] == ' ') { cut += d; skip = 1; break; }
			}
			for (i = 0; i < cut && i < 12; i++)
				l1[i] = t[i];
			l1[i] = 0;
			for (i = 0; cut + skip + i < len && i < 12; i++)
				l2[i] = t[cut + skip + i];
			l2[i] = 0;
			ST7735S_FillRect(10, (uint8_t)(y + 14), 108, 14, COLOR_BG);
			UI_PrintString(l1, 10, (uint8_t)(y + 14), freq_color(s, active), COLOR_BG, 1);
			UI_PrintString(l2, 10, (uint8_t)(y + 22), freq_color(s, active), COLOR_BG, 1);
		}
	}
	if (full || active != was || s->am != o->am || s->narrow != o->narrow || s->high_power != o->high_power
	    || s->shift != o->shift || s->tone_kind != o->tone_kind || differ(s->tone, o->tone))
		draw_tags(s, (uint8_t)(y + 35), active);
}

/* ------------------------------------------------------------------------------------------- whole screen */
void UI_MainDraw(const UiMain_t *v, bool full)
{
	if (full)
		ST7735S_FillRect(0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_BG);
	draw_status(v, full);
	draw_side(v, 0, full);
	draw_side(v, 1, full);
	if (full || differ(v->bottom, s_shown.bottom) || v->bottom_color != s_shown.bottom_color)
		UI_PrintPadded(v->bottom, 0, BOTTOM_Y + 2, v->bottom_color, COLOR_BG, 21);
	s_shown = *v;
}
