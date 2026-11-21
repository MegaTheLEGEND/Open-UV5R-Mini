/* Open-UV5R-Mini: which scan code does what. The digits are certain (stock key table, RE-NOTES); the function keys
 * are guesses until someone presses each one on a radio running the key test (menu item KEY TEST). Change them here
 * only. */
#ifndef APP_KEYS_H
#define APP_KEYS_H

#include "driver/keyboard.h"

#define KEY_ACT_MENU  KEY_FN1      /* 4th column, top row (guess) */
#define KEY_ACT_UP    KEY_FN2      /* 4th column, 2nd row (guess) */
#define KEY_ACT_DOWN  KEY_FN3      /* 4th column, 3rd row (guess) */
#define KEY_ACT_EXIT  KEY_FN4      /* 4th column, bottom row (guess) */
#define KEY_ACT_BW    KEY_STAR     /* toggles wide / narrow */
#define KEY_ACT_LAMP  KEY_HASH     /* flashlight on / off */

/* digit value of a key, or -1 */
static inline int KEY_Digit(KEY_Code_t k)
{
	switch (k) {
	case KEY_0: return 0;
	case KEY_1: return 1;
	case KEY_2: return 2;
	case KEY_3: return 3;
	case KEY_4: return 4;
	case KEY_5: return 5;
	case KEY_6: return 6;
	case KEY_7: return 7;
	case KEY_8: return 8;
	case KEY_9: return 9;
	default:    return -1;
	}
}

#endif
