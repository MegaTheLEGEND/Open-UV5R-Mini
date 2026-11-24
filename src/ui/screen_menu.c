/* Open-UV5R-Mini: the menu screen. See ui/screen_menu.h. */
#include "ui/screen_menu.h"
#include "driver/st7735s.h"
#include "ui/helper.h"
#include "ui/theme.h"

static int s_top = -1;                 /* the first row drawn last time; -1 = rows need clearing */

uint8_t UI_MenuTop(uint8_t sel)
{
	return sel < UI_MENU_ROWS ? 0 : (uint8_t)(sel - UI_MENU_ROWS + 1);
}

void UI_MenuDraw(const UiMenu_t *m, bool full)
{
	char n[4];

	if (full) {
		ST7735S_FillRect(0, 0, 128, 128, COLOR_BG);
		ST7735S_FillRect(0, 0, 128, 11, COLOR_BAR);
		UI_PrintString("MENU", 2, 2, COLOR_TEXT, COLOR_BAR, 1);
		ST7735S_FillRect(0, 98, 128, 1, COLOR_DIM);
		s_top = -1;
	}
	/* NN/MM, top right */
	{
		char c[8], *p = c;
		const char *q = UI_FormatDec(n, (uint32_t)m->sel + 1u, 2);
		*p++ = q[0]; *p++ = q[1]; *p++ = '/';
		q = UI_FormatDec(n, m->count, 2);
		*p++ = q[0]; *p++ = q[1]; *p = 0;
		UI_PrintString(c, 96, 2, m->sel >= m->hidden_from ? COLOR_WARN : COLOR_DIM, COLOR_BAR, 1);
	}
	if (m->top != s_top) {
		s_top = m->top;
		ST7735S_FillRect(0, 12, 128, 12 * UI_MENU_ROWS + 2, COLOR_BG);
	}
	for (int r = 0; r < UI_MENU_ROWS && m->top + r < m->count; r++) {
		int i = m->top + r;
		uint8_t y = (uint8_t)(15 + 12 * r);
		bool sel = i == m->sel;
		uint16_t bg = sel ? COLOR_ACCENT : COLOR_BG;
		ST7735S_FillRect(0, (uint8_t)(y - 2), 128, 11, bg);
		UI_PrintString(UI_FormatDec(n, (uint32_t)i + 1u, 2), 3, y,
		               sel ? COLOR_TEXT : i >= m->hidden_from ? COLOR_WARN : COLOR_DIM, bg, 1);
		UI_PrintString(m->name[r] ? m->name[r] : "", 22, y, COLOR_TEXT, bg, 1);
	}
	/* the selected item's value, large if it fits */
	{
		const char *v = m->value ? m->value : "";
		unsigned len = 0;
		uint16_t fg = m->edit ? COLOR_RX : COLOR_WARN;
		while (v[len])
			len++;
		ST7735S_FillRect(0, 100, 128, 28, COLOR_BG);
		if (len <= 10)
			UI_PrintString(v, (uint8_t)((128 - 12 * len) / 2), 107, fg, COLOR_BG, 2);
		else
			UI_PrintString(v, (uint8_t)(len < 21 ? (128 - 6 * len) / 2 : 0), 110, fg, COLOR_BG, 1);
	}
}
