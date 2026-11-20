/* Open-UV5R-Mini: 5x7 font, ASCII 0x20..0x5F (lowercase is drawn as uppercase). */
#ifndef UI_FONT_H
#define UI_FONT_H

#include <stdint.h>

#define FONT_FIRST 0x20
#define FONT_LAST  0x5F

extern const uint8_t gFont5x7[FONT_LAST - FONT_FIRST + 1][5];

#endif
