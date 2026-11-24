/* Open-UV5R-Mini: the main screen. Both sides, A and B, are shown at once, each in its own panel.
 *
 *   y 0-10    status bar: volume, squelch, dual watch, scan, lamp, warning, battery
 *   y 13-60   side A: badge, channel label, receive/transmit icon and S-meter; the frequency; mode tags
 *   y 64-111  side B: the same
 *   y 116-127 bottom line: what is being typed, DTMF heard, step, key hints
 *
 * The app fills a UiMain_t with what should be on screen and calls UI_MainDraw; only what changed is drawn again
 * (the LCD is slow). Nothing here reads the radio: the screen can be drawn on the PC (tests/ui_preview.py). */
#ifndef UI_SCREEN_MAIN_H
#define UI_SCREEN_MAIN_H

#include <stdbool.h>
#include <stdint.h>

enum { UI_STATE_IDLE = 0, UI_STATE_RX = 1, UI_STATE_TX = 2 };
enum { UI_TONE_NONE = 0, UI_TONE_CTCSS = 1, UI_TONE_DCS = 2 };

#define UI_METER_BARS 7

typedef struct {
	uint32_t freq;          /* receive frequency, 10 Hz units */
	char     big[13];       /* shown instead of the frequency when set (channel name or number): large up to 9
	                         * characters, longer on two smaller lines */
	char     label[13];     /* small text by the badge: channel name or "CH-012"; "" shows "VFO" */
	char     tone[14];      /* tone text ("88.5", "D023N", "88.5/D023N"); "" when none */
	uint8_t  tone_kind;     /* UI_TONE_* */
	int8_t   shift;         /* repeater shift: -1, 0, +1 */
	bool     am, narrow, high_power;
	uint8_t  state;         /* UI_STATE_IDLE, UI_STATE_RX, UI_STATE_TX */
	int8_t   meter;         /* 0..UI_METER_BARS bars lit; -1 = this side isn't tuned now, no meter */
} UiSide_t;

typedef struct {
	UiSide_t side[2];       /* 0 = A, 1 = B */
	uint8_t  active;        /* the side keys act on */
	char     entry[10];     /* typed digits, shown large on the active side; "" when not typing */
	uint8_t  volume, squelch;
	bool     dual_watch, scan, scan_hold, lamp, strobe, warning, locked;
	int8_t   battery;       /* 0..4 bars, -1 unknown */
	bool     battery_low, battery_hide;
	char     bottom[22];
	uint16_t bottom_color;
} UiMain_t;

/* Draw what differs from the last call; full = everything (after another screen was shown). */
void UI_MainDraw(const UiMain_t *v, bool full);

/* "145.52000" from 10 Hz units (9 characters, a leading space below 100 MHz). */
char *UI_FormatFreq(char *buf, uint32_t freq);

#endif
