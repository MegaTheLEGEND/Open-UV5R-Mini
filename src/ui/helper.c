/* Open-UV5R-Mini: text drawing. */
#include "ui/helper.h"
#include "driver/gpio.h"
#include "driver/st7735s.h"
#include "board.h"
#include "ui/font.h"

static void draw_char(char c, uint8_t x, uint8_t y, uint16_t fg, uint16_t bg, uint8_t scale)
{
	if (c >= 'a' && c <= 'z')
		c -= 'a' - 'A';
	if (c < FONT_FIRST || c > FONT_LAST)
		c = '?';
	const uint8_t *glyph = gFont5x7[c - FONT_FIRST];

	/* 6 columns (5 + 1 spacing) by 8 rows, row-major as the panel expects */
	ST7735S_SetWindow(x, y, 6 * scale, 8 * scale);
	for (uint8_t row = 0; row < 8 * scale; row++) {
		for (uint8_t col = 0; col < 6 * scale; col++) {
			uint8_t gc = col / scale, gr = row / scale;
			int on = gc < 5 && (glyph[gc] >> gr) & 1;
			ST7735S_PushPixel(on ? fg : bg);
		}
	}
	ST7735S_End();
}

uint8_t UI_PrintString(const char *str, uint8_t x, uint8_t y, uint16_t fg, uint16_t bg, uint8_t scale)
{
	while (*str && x + 6 * scale <= LCD_WIDTH) {
		draw_char(*str++, x, y, fg, bg, scale);
		x += 6 * scale;
	}
	return x;
}

void UI_DrawIcon(const Icon_t *icon, uint8_t x, uint8_t y, uint16_t fg, uint16_t bg)
{
	uint8_t stride = (uint8_t)((icon->w + 7u) / 8u);

	if (x + icon->w > LCD_WIDTH || y + icon->h > LCD_HEIGHT)
		return;
	ST7735S_SetWindow(x, y, icon->w, icon->h);
	for (uint8_t row = 0; row < icon->h; row++)
		for (uint8_t col = 0; col < icon->w; col++)
			ST7735S_PushPixel((icon->bits[row * stride + col / 8u] >> (7u - col % 8u)) & 1u ? fg : bg);
	ST7735S_End();
}

uint8_t UI_PrintPadded(const char *str, uint8_t x, uint8_t y, uint16_t fg, uint16_t bg, uint8_t width)
{
	char p[22];
	uint8_t i = 0;

	for (; i < width && i < sizeof p - 1 && str[i]; i++)
		p[i] = str[i];
	for (; i < width && i < sizeof p - 1; i++)
		p[i] = ' ';
	p[i] = 0;
	return UI_PrintString(p, x, y, fg, bg, 1);
}

char *UI_FormatDec(char *buf, uint32_t value, uint8_t digits)
{
	buf[digits] = 0;
	while (digits--) {
		buf[digits] = '0' + value % 10;
		value /= 10;
	}
	return buf;
}

char *UI_FormatHex(char *buf, uint32_t value, uint8_t digits)
{
	buf[digits] = 0;
	while (digits--) {
		buf[digits] = "0123456789ABCDEF"[value & 15];
		value >>= 4;
	}
	return buf;
}
