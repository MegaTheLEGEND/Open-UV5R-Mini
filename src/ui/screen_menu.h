/* Open-UV5R-Mini: the menu screen, laid out like the UV-K5 open firmware's menu.
 *
 *   y 0-10    title bar: "MENU", and the selected item's number out of the total ("38/39") at the right
 *   y 13-96   UI_MENU_ROWS numbered rows; the selected one highlighted; hidden items' numbers in yellow
 *   y 98      rule
 *   y 100-127 the selected item's value, large if it fits (green while it is being changed)
 *
 * The app fills a UiMenu_t and calls UI_MenuDraw; nothing here reads the radio, so the screen can be drawn on the
 * PC (tests/ui_preview.py). */
#ifndef UI_SCREEN_MENU_H
#define UI_SCREEN_MENU_H

#include <stdbool.h>
#include <stdint.h>

#define UI_MENU_ROWS 7

typedef struct {
	uint8_t     count;                 /* items in the list */
	uint8_t     sel;                   /* selected item, 0-based */
	uint8_t     top;                   /* first item on screen */
	uint8_t     hidden_from;           /* items from here on are the hidden ones (count if none shown) */
	bool        edit;                  /* the value is being changed */
	const char *name[UI_MENU_ROWS];    /* names of items top .. top + UI_MENU_ROWS - 1 */
	const char *value;                 /* the selected item's value */
} UiMenu_t;

/* The first item to show so that sel is on screen. */
uint8_t UI_MenuTop(uint8_t sel);
/* Draw the menu; full = everything (after another screen was shown). */
void    UI_MenuDraw(const UiMenu_t *m, bool full);

#endif
