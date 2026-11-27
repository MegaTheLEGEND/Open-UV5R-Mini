/* Open-UV5R-Mini: the one typing service for every text field (ui/textentry.c), keypad letters as on a phone
 * (Sean, 2026-11-27: "a unified typing service for T9 typing. Default to letters. F goes to numbers.").
 *
 *   letters (ABC, the start)  2 = A B C 2, 3 = D E F 3 ... 9 = W X Y Z 9: the same key again within TEXT_TAP_MS
 *                             gives the next letter, another key (or a pause) moves on. 0 = space 0, 1 = . - / 1 ...
 *   numbers (123)             each key types its digit
 *   F (the # key)             letters <-> numbers
 *   *                         deletes the last character
 *   UP / DOWN                 changes the last character through every character the field allows
 *   MENU                      done; EXIT cancels (the caller keeps or drops the text)
 *
 * The kind of field limits what can be typed: a call sign takes capitals, digits and '-'; a name also takes a space
 * and . / + # * ( ); free text takes any character the font has. Pure C, no drawing: the caller shows TEXT_Line and
 * TEXT_ModeName (tests/textentry_host.py runs it on the PC). */
#ifndef UI_TEXTENTRY_H
#define UI_TEXTENTRY_H

#include <stdbool.h>
#include <stdint.h>
#include "driver/keyboard.h"

#define TEXT_TAP_MS 1000u

typedef enum { TEXT_CALL, TEXT_NAME, TEXT_FREE } TextKind_t;
typedef enum { TEXT_EDITING, TEXT_DONE, TEXT_CANCEL } TextResult_t;

typedef struct {
	char       *buf;                     /* the text, always 0-terminated */
	uint8_t     max, len;                /* buf holds max characters + the 0 */
	uint8_t     kind;                    /* TextKind_t */
	bool        numbers;                 /* 123 mode (F); letters otherwise */
	int8_t      tap_key;                 /* the digit last pressed, -1 = none (the next press starts a character) */
	uint8_t     tap_i;                   /* which of its characters */
	uint32_t    tap_ms;                  /* when */
} TextEntry_t;

/* Starts editing buf (max characters) with what is in it already; letters mode. */
void         TEXT_Start(TextEntry_t *t, char *buf, uint8_t max, TextKind_t kind);
/* One key press; now_ms: a free-running ms clock. */
TextResult_t TEXT_Key(TextEntry_t *t, KEY_Code_t k, uint32_t now_ms);
/* "ABC" or "123" */
const char  *TEXT_ModeName(const TextEntry_t *t);
/* The last cols characters of the text with a '_' cursor after them while there is room: what to show. */
void         TEXT_Line(const TextEntry_t *t, char *out, uint8_t cols);
/* An app is typing: app.c gives it the # key (F) instead of using it as its own F. */
extern bool  gTextActive;
void         TEXT_End(void);             /* the field closed (also done by TEXT_DONE and TEXT_CANCEL) */

#endif
