/* Open-UV5R-Mini: the remote-control monitor on the cable (ENABLE_DEBUGGING builds only; Sean asked 2026-11-27 for
 * "full control" from the PC while the radio runs).
 *
 * It shares the cable with CHIRP: every byte goes to both, and a monitor command is a text line that starts with '!'
 * (CHIRP's own bytes never form one). Works on every screen, in the main loop (not while transmitting).
 * Replies are lines starting with '@', ending with "@ok" or "@err ...". Unasked messages start with '#'.
 *
 *   !ping                     @pong <build>
 *   !state                    screen, app, frequency, receiver state, RSSI, squelch, uptime
 *   !screen                   the text on the LCD as drawn (a shadow of every character drawn, rows of 4 px)
 *   !r A [N]                  N 32-bit words of memory from A (hex; N up to 64)
 *   !w A V                    write a 32-bit word
 *   !rb A N                   N bytes (up to 256)
 *   !bk R [V]                 read (or write) BK4819 register R (hex)
 *   !bkdump                   BK4819 registers 00-7F
 *   !key K [MS]               hold key K for MS ms (default 120) as if pressed: 0-9 * # MENU UP DOWN EXIT VM AB LAMP EQ;
 *                             MS 800 gives the held (long) action
 *   !app ...                  to the open app (the APRS app: !app help)
 *   !events 0|1               '#' messages on or off (on at start)
 */
#ifndef APP_MONITOR_H
#define APP_MONITOR_H

#include <stdbool.h>
#include <stdint.h>
#include "app/apps.h"

#if DEBUGGING
void MON_Feed(const uint8_t *b, int n);  /* bytes from the cable */
void MON_Line(const char *s);            /* one line to the PC, "\r\n" added */
void MON_Event(const char *kind, const char *text);   /* "#kind text", when events are on */
/* the LCD text shadow (ui/helper.c, driver/st7735s.c) */
void MON_Char(char c, uint8_t x, uint8_t y, uint8_t scale);
void MON_Clear(uint8_t x, uint8_t y, uint8_t w, uint8_t h);
/* an app's own commands ("!app ..."): it answers with MON_Line and returns false for "@err" */
extern bool (*gMonApp)(const char *args);
/* from app.c */
void APP_MonState(char *out, unsigned max);
void APP_MonKey(KEY_Code_t k, uint16_t ms);
#else
#define MON_Event(k, t) ((void)0)
#endif

#endif
