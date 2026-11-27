/* Open-UV5R-Mini: the menu screen. See ui/screen_menu.h. */
#include "ui/screen_menu.h"
#include "driver/st7735s.h"
#include "ui/helper.h"
#include "ui/theme.h"
#include "ui/status.h"

static int s_top = -1;                 /* the first row drawn last time; -1 = rows need clearing */

static unsigned text_len(const char *s)
{
	unsigned n = 0;
	while (s && s[n])
		n++;
	return n;
}

uint8_t UI_MenuTop(uint8_t sel)
{
	return sel < UI_MENU_ROWS ? 0 : (uint8_t)(sel - UI_MENU_ROWS + 1);
}

void UI_MenuDraw(const UiMenu_t *m, bool full)
{
	char n[4];

	if (full) {
		ST7735S_FillRect(0, 0, 128, 128, COLOR_BG);
		UI_TitleBar(m->title ? m->title : "MENU", 0);
		ST7735S_FillRect(0, 98, 128, 1, COLOR_DIM);
		s_top = -1;
	}
	UI_TitleRight(m->corner, m->corner_warn ? COLOR_WARN : COLOR_DIM);
	if (m->top != s_top) {
		s_top = m->top;
		ST7735S_FillRect(0, 12, 128, 12 * UI_MENU_ROWS + 2, COLOR_BG);
	}
	for (int r = 0; r < UI_MENU_ROWS && m->top + r < m->count; r++) {
		uint8_t y = (uint8_t)(15 + 12 * r);
		bool sel = m->top + r == m->sel;
		uint16_t bg = sel ? COLOR_ACCENT : COLOR_BG;
		ST7735S_FillRect(0, (uint8_t)(y - 2), 128, 11, bg);
		if (m->num[r])
			UI_PrintString(UI_FormatDec(n, m->num[r], 2), 3, y,
			               sel ? COLOR_TEXT : m->hidden[r] ? COLOR_WARN : COLOR_DIM, bg, 1);
		UI_PrintString(m->name[r] ? m->name[r] : "", m->num[r] ? 22 : 6, y, COLOR_TEXT, bg, 1);   /* no number: from the left */
		if (m->right[r])
			UI_PrintString(m->right[r], (uint8_t)(125 - 6 * text_len(m->right[r])), y, sel ? COLOR_TEXT : COLOR_DIM,
			               bg, 1);
	}
	/* the selected row's value, large if it fits */
	{
		const char *v = m->value ? m->value : "";
		unsigned len = text_len(v);
		uint16_t fg = m->edit ? COLOR_RX : COLOR_WARN;
		ST7735S_FillRect(0, 100, 128, 28, COLOR_BG);
		if (len <= 10) {
			uint8_t x = (uint8_t)((128 - 12 * len) / 2);
			UI_PrintString(v, x, 107, fg, COLOR_BG, 2);
			if (m->cursor >= 0)
				ST7735S_FillRect((uint8_t)(x + 12 * m->cursor), 123, 10, 2, COLOR_TEXT);
		} else {
			uint8_t x = (uint8_t)(len < 21 ? (128 - 6 * len) / 2 : 0);
			UI_PrintString(v, x, 110, fg, COLOR_BG, 1);
			if (m->cursor >= 0)
				ST7735S_FillRect((uint8_t)(x + 6 * m->cursor), 119, 5, 1, COLOR_TEXT);
		}
	}
}
