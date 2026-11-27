/* Open-UV5R-Mini: the shared top bar. See ui/status.h. */
#include "ui/status.h"
#include "driver/st7735s.h"
#include "ui/helper.h"
#include "ui/icons.h"
#include "ui/theme.h"

UiStatus_t gUiStatus = { .battery = -1 };

static UiStatus_t s_drawn;
static bool       s_bar;                 /* a UI_TitleBar bar is on screen */
static uint8_t    s_title_end;           /* x after the title */

static unsigned text_len(const char *s)
{
	unsigned n = 0;
	while (s && s[n])
		n++;
	return n;
}

static int differ(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a != *b;
}

void UI_DrawFKey(bool on)
{
	if (!on) {
		ST7735S_FillRect(1, 1, 9, 9, COLOR_BAR);
		return;
	}
	ST7735S_FillRect(1, 1, 9, 9, COLOR_WARN);   /* "F" in a yellow tag, corners off */
	UI_PrintString("F", 3, 2, COLOR_BG, COLOR_WARN, 1);
	ST7735S_FillRect(1, 1, 1, 1, COLOR_BAR);
	ST7735S_FillRect(9, 1, 1, 1, COLOR_BAR);
	ST7735S_FillRect(1, 9, 1, 1, COLOR_BAR);
	ST7735S_FillRect(9, 9, 1, 1, COLOR_BAR);
}

void UI_DrawBattery(const UiStatus_t *v)
{
	const uint8_t x = 109;
	uint16_t c = v->battery_low ? COLOR_TX : COLOR_RX;

	ST7735S_FillRect(102, 1, (uint8_t)(LCD_WIDTH - 102), 9, COLOR_BAR);   /* the text is wider than the icon */
	if (v->battery_hide)
		return;
	if (v->battery_text[0]) {               /* percent or volts, right-aligned */
		UI_PrintString(v->battery_text, (uint8_t)(LCD_WIDTH - 1 - 6 * text_len(v->battery_text)), 2,
		               v->battery_low ? COLOR_TX : COLOR_TEXT, COLOR_BAR, 1);
		return;
	}
	UI_DrawIcon(&ICON_STATUS_BATTERY, x, 1, v->battery_low ? COLOR_TX : COLOR_DIM, COLOR_BAR);
	for (int i = 0; i < 4; i++)       /* four cells of 2 x 5 inside the frame */
		ST7735S_FillRect((uint8_t)(x + 2 + 3 * i), 3, 2, 5, i < v->battery ? c : COLOR_BAR);
}

void UI_TitleRight(const char *right, uint16_t color)
{
	unsigned n = text_len(right), room;
	char b[16];

	ST7735S_FillRect(s_title_end, 0, (uint8_t)(UI_BAR_RIGHT_END + 2u - s_title_end), UI_BAR_H, COLOR_BAR);
	room = (UI_BAR_RIGHT_END - s_title_end - 3u) / 6u;
	if (!n || !room)
		return;
	if (n > room)
		n = room;
	if (n > sizeof b - 1u)
		n = sizeof b - 1u;
	for (unsigned i = 0; i < n; i++)
		b[i] = right[i];
	b[n] = 0;
	UI_PrintString(b, (uint8_t)(UI_BAR_RIGHT_END - 6u * n), 2, color, COLOR_BAR, 1);
}

void UI_TitleBar(const char *title, const char *right)
{
	unsigned n = text_len(title);

	if (n > 14u)
		n = 14u;
	ST7735S_FillRect(0, 0, LCD_WIDTH, UI_BAR_H, COLOR_BAR);
	UI_PrintString(title, 12, 2, COLOR_TEXT, COLOR_BAR, 1);
	s_title_end = (uint8_t)(12u + 6u * n);
	if (s_title_end > UI_BAR_RIGHT_END)
		s_title_end = UI_BAR_RIGHT_END;
	if (right)
		UI_TitleRight(right, COLOR_DIM);
	s_drawn = gUiStatus;
	UI_DrawFKey(s_drawn.fkey);
	UI_DrawBattery(&s_drawn);
	s_bar = true;
}

void UI_StatusRefresh(void)
{
	const UiStatus_t *v = &gUiStatus;

	if (!s_bar)
		return;
	if (v->fkey != s_drawn.fkey)
		UI_DrawFKey(v->fkey);
	if (v->battery != s_drawn.battery || v->battery_low != s_drawn.battery_low
	    || v->battery_hide != s_drawn.battery_hide || differ(v->battery_text, s_drawn.battery_text))
		UI_DrawBattery(v);
	s_drawn = *v;
}

void UI_StatusHide(void)
{
	s_bar = false;
}
