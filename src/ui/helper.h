/* Open-UV5R-Mini: text and icon drawing. */
#ifndef UI_HELPER_H
#define UI_HELPER_H

#include <stdint.h>
#include "ui/icons.h"

/* Draw `str` at pixel (x, y), each font pixel `scale` x `scale` screen pixels; returns the next x. */
uint8_t UI_PrintString(const char *str, uint8_t x, uint8_t y, uint16_t fg, uint16_t bg, uint8_t scale);
/* `str` at scale 1, then spaces to `width` characters, so older, longer text is covered without a clear. */
uint8_t UI_PrintPadded(const char *str, uint8_t x, uint8_t y, uint16_t fg, uint16_t bg, uint8_t width);
/* A 1-bit icon (src/ui/icons.h, made from assets/icons/): lit pixels in fg, the rest in bg. */
void    UI_DrawIcon(const Icon_t *icon, uint8_t x, uint8_t y, uint16_t fg, uint16_t bg);
/* Format a number as `digits` decimal digits (zero padded) or hex into buf; returns buf. */
char   *UI_FormatDec(char *buf, uint32_t value, uint8_t digits);
char   *UI_FormatHex(char *buf, uint32_t value, uint8_t digits);

#endif
