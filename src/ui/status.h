/* Open-UV5R-Mini: the top bar every screen shares (Sean, 2026-11-27: "all apps should have a fairly similar display,
 * battery in the top right, function indicator to the left").
 *
 *   x 1-9      F, when the function key (#) is on
 *   x 12-      the screen's title
 *   ... -99    the screen's own short text, right-aligned, cut if it would run into the title
 *   x 102-127  the battery: the icon with its bars, or "84%" / "4.1V", as chosen in the menu
 *
 * The app fills gUiStatus each tick. UI_TitleBar draws the whole bar; UI_StatusRefresh draws again only the F and
 * battery that changed since, and only while a bar drawn by UI_TitleBar is on screen (UI_StatusHide forgets it,
 * for screens with no bar). The main screen draws its own bar with UI_DrawFKey and UI_DrawBattery. */
#ifndef UI_STATUS_H
#define UI_STATUS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	bool    fkey;                    /* F on */
	int8_t  battery;                 /* 0..4 bars, -1 unknown */
	bool    battery_low, battery_hide;   /* hide: the blink when low */
	char    battery_text[5];         /* "84%" or "4.1V" instead of the icon; "" = the icon */
} UiStatus_t;

extern UiStatus_t gUiStatus;

#define UI_BAR_H        11u
#define UI_BAR_RIGHT_END 99u             /* a screen's right text ends before this */

void UI_TitleBar(const char *title, const char *right);
void UI_TitleRight(const char *right, uint16_t color);   /* the right text again, the rest left as it is */
void UI_StatusRefresh(void);
void UI_StatusHide(void);

void UI_DrawFKey(bool on);
void UI_DrawBattery(const UiStatus_t *s);

#endif
