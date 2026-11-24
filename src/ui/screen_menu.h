/* Open-UV5R-Mini: the menu screen, laid out like the UV-K5 open firmware's menu.
 *
 *   y 0-10    title bar: the page ("MENU", "CHANNEL", ...) and, at the right, the selected item's number out of the
 *             total ("05/43") or the number being typed
 *   y 13-96   UI_MENU_ROWS rows: item number, name, and small text at the right (a section's number range); the
 *             selected row highlighted; hidden items' numbers in yellow
 *   y 98      rule
 *   y 100-127 the selected item's value, large if it fits (green while it is being changed), with a cursor under one
 *             character while a name is edited
 *
 * The app fills a UiMenu_t and calls UI_MenuDraw; nothing here reads the radio, so the screen can be drawn on the
 * PC (tests/ui_preview.py). */
#ifndef UI_SCREEN_MENU_H
#define UI_SCREEN_MENU_H

#include <stdbool.h>
#include <stdint.h>

#define UI_MENU_ROWS 7

typedef struct {
	const char *title;                 /* "MENU", or the section's name */
	char        corner[8];             /* top right: "05/43", or the number being typed ("1_") */
	bool        corner_warn;           /* corner in yellow (a hidden item, or typing) */
	uint8_t     count;                 /* rows in the list */
	uint8_t     sel;                   /* selected row, 0-based */
	uint8_t     top;                   /* first row on screen */
	bool        edit;                  /* the value is being changed */
	const char *name[UI_MENU_ROWS];    /* rows top .. top + UI_MENU_ROWS - 1 */
	uint8_t     num[UI_MENU_ROWS];     /* the item number at the left; 0 = none */
	bool        hidden[UI_MENU_ROWS];  /* a hidden item: number in yellow */
	const char *right[UI_MENU_ROWS];   /* small text at the right, or 0 */
	const char *value;                 /* the selected row's value */
	int8_t      cursor;                /* the value's character with a cursor under it; -1 = none */
} UiMenu_t;

/* The first row to show so that sel is on screen. */
uint8_t UI_MenuTop(uint8_t sel);
/* Draw the menu; full = everything (after another screen or page was shown). */
void    UI_MenuDraw(const UiMenu_t *m, bool full);

#endif
