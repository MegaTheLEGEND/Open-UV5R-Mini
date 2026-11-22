/* Open-UV5R-Mini: keypad. Codes are the stock firmware's scan codes (table at 0x01E65E94). */
#ifndef DRIVER_KEYBOARD_H
#define DRIVER_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	KEY_DIRECT0 = 0,  /* direct to GND on PC2 (unknown key) */
	KEY_DIRECT3 = 2,  /* direct on PC5 (unknown) */
	KEY_DIRECT1 = 3,  /* direct on PC3 (unknown) */
	KEY_DIRECT2 = 4,  /* direct on PC4 (unknown) */
	KEY_STAR    = 5,  /* inferred */
	KEY_0       = 6,  /* inferred */
	KEY_HASH    = 7,  /* inferred */
	KEY_FN4     = 8,  /* 4th column, PA6 row (unknown) */
	KEY_1       = 9,
	KEY_2       = 10,
	KEY_3       = 11,
	KEY_FN1     = 12, /* 4th column, PA9 row (unknown) */
	KEY_4       = 13,
	KEY_5       = 14,
	KEY_6       = 15,
	KEY_FN2     = 16, /* 4th column, PA8 row (unknown) */
	KEY_7       = 17,
	KEY_8       = 18,
	KEY_9       = 19,
	KEY_FN3     = 20, /* 4th column, PA7 row (unknown) */
	KEY_INVALID = 0xFF,
} KEY_Code_t;

void       KEYBOARD_Init(void);
KEY_Code_t KEYBOARD_Poll(void);
uint8_t    KEYBOARD_RawColumns(unsigned step);
bool       KEYBOARD_IsPttPressed(void);
bool       KEYBOARD_IsPtt2Pressed(void);

#endif
