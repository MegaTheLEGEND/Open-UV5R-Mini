/* Open-UV5R-Mini: which scan code does what. The digits are certain (stock key table, RE-NOTES); the function keys
 * were checked on a radio with the key test (ROWS readout). Change them here
 * only. */
#ifndef APP_KEYS_H
#define APP_KEYS_H

#include "driver/keyboard.h"

#define KEY_ACT_MENU  KEY_MENU
#define KEY_ACT_UP    KEY_UP
#define KEY_ACT_DOWN  KEY_DOWN
#define KEY_ACT_EXIT  KEY_EXIT
#define KEY_ACT_BW    KEY_STAR     /* toggles wide / narrow */
#define KEY_ACT_LAMP  KEY_LAMP     /* flashlight on / off */
#define KEY_ACT_AB    KEY_AB       /* side A / B */
#define KEY_ACT_VM    KEY_VM       /* VFO / channel mode */
#define KEY_ACT_LOCK  ((KEY_Code_t)0xFE)   /* not a key: # held for KEY_LOCK_MS (app.c key_event) */
#define KEY_LOCK_MS      1000u
/* a key held this long does its second job (app.c key_event): KEY_LONG(key); a shorter press acts when let go */
#define KEY_LONG_MS      500u
#define KEY_LONG(k)      ((KEY_Code_t)((unsigned)(k) | 0x40u))
#define KEY_IS_LONG(k)   (((unsigned)(k) & 0xC0u) == 0x40u)
#define KEY_BASE(k)      (KEY_IS_LONG(k) ? (KEY_Code_t)((unsigned)(k) & 0x3Fu) : (k))
#define KEY_AUTOLOCK_MS  15000u            /* auto lock (stock setting 0x10) after this long with no key; our choice */

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
