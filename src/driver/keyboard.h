/* Open-UV5R-Mini: keypad. Codes are the stock firmware's scan codes (table at 0x01E65E94). */
#ifndef DRIVER_KEYBOARD_H
#define DRIVER_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
	/* Confirmed on a radio (key test ROWS readout, 2026-11-21). Keys wired to GND: */
	KEY_EQUALS  = 0,  /* PC2 to GND: the = key (stock checks it at power-on for the update mode, 0x01E492C0) */
	KEY_DIRECT0 = KEY_EQUALS,
	KEY_VM      = 2,  /* PC5 */
	KEY_AB      = 3,  /* PC3 */
	KEY_LAMP    = 4,  /* PC4, flashlight key */
	/* row PA6 */
	KEY_MENU    = 5,  /* PC5 */
	KEY_UP      = 6,  /* PC4 */
	KEY_DOWN    = 7,  /* PC3 */
	KEY_EXIT    = 8,  /* PC2 */
	/* row PA9 */
	KEY_1       = 9,
	KEY_2       = 10,
	KEY_3       = 11,
	KEY_STAR    = 12,
	/* row PA8 */
	KEY_4       = 13,
	KEY_5       = 14,
	KEY_6       = 15,
	KEY_0       = 16,
	/* row PA7 */
	KEY_7       = 17,
	KEY_8       = 18,
	KEY_9       = 19,
	KEY_HASH    = 20,
	KEY_INVALID = 0xFF,
} KEY_Code_t;

void       KEYBOARD_Init(void);
KEY_Code_t KEYBOARD_Poll(void);
uint8_t    KEYBOARD_RawColumns(unsigned step);
bool       KEYBOARD_IsPttPressed(void);
bool       KEYBOARD_IsPtt2Pressed(void);

#endif
