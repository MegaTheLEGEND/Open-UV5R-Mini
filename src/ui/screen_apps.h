/* Open-UV5R-Mini: the apps' screens (MENU -> APPS). Like the other screens they are drawn from what the app hands
 * them and never read the radio, so tests/ui_preview.py can draw them on the PC.
 *
 * Spectrum (128 x 128):
 *   y 0-10    title bar: "SPECTRUM" and the step ("12.5K")
 *   y 13-96   UI_SPECTRUM_N columns 2 px wide, the level as a bar; the centre column marked, the peak in yellow
 *   y 98-107  the left edge's offset ("-400K"), the centre frequency, the right edge's offset
 *   y 110-127 the peak: frequency and level, or "FROZEN"
 *
 * Signal meter (the fox hunt):
 *   y 0-10    title bar: the mode ("MR 005", "VFO") and the channel's name (or the frequency in VFO mode)
 *   y 14-45   the level, large ("-87"), with "dBm", and the S-units ("S7", "S9+20")
 *   y 48-57   a bar from -130 to -40 dBm; the peak as a tick
 *   y 60-111  history: one column a sample, newest at the right
 *   y 116-127 the peak level and the tick sound; the digits being typed; why transmit was refused
 *
 * Info page (system info): a title and up to 9 label / value rows. */
#ifndef UI_SCREEN_APPS_H
#define UI_SCREEN_APPS_H

#include <stdbool.h>
#include <stdint.h>

#define UI_SPECTRUM_N   64
#define UI_SPECTRUM_TOP 13
#define UI_SPECTRUM_H   84               /* rows for the bars */

/* The frame: title bar, frequency labels and the peak line. */
void UI_SpectrumFrame(const char *step, const char *left, const char *centre, const char *right);
void UI_SpectrumPeak(const char *text, bool warn);
/* Column i: a bar h rows high (0..UI_SPECTRUM_H); peak = in the peak colour. */
void UI_SpectrumBar(uint8_t i, uint8_t h, bool peak);

#define UI_METER_HIST 124

typedef struct {
	char    left[8];                     /* "MR 005", "VFO" */
	char    right[13];                   /* the channel's name, or "145.52000" */
	char    bottom[22];                  /* typed digits or a TX refusal; "" = the peak line */
	bool    tx;                          /* transmitting: "TX" instead of the level */
	int16_t dbm;                         /* now */
	int16_t peak_dbm;
	char    sunits[7];                   /* "S9+20" */
	uint8_t hist[UI_METER_HIST];         /* heights 0..50, oldest first */
	uint8_t hist_n;                      /* samples in hist */
	bool    tick_sound;
	bool    open;                        /* squelch open: the level in green */
} UiMeter_t;

void UI_MeterDraw(const UiMeter_t *m, bool full);

#define UI_INFO_ROWS 9

typedef struct {
	const char *title;
	const char *label[UI_INFO_ROWS];     /* 0 = no row */
	const char *value[UI_INFO_ROWS];
	const char *hint;                    /* bottom line */
} UiInfo_t;

void UI_InfoDraw(const UiInfo_t *p, bool full);

/* A small game board: cells of UI_GAME_CELL pixels under the title bar. */
#define UI_GAME_CELL 6
#define UI_GAME_W    21
#define UI_GAME_H    19
void UI_GameFrame(const char *title);
void UI_GameCell(uint8_t x, uint8_t y, uint16_t color);
void UI_GameScore(const char *text);

/* FM broadcast radio:
 *   y 0-10    title bar: "FM RADIO" and STEREO / MONO
 *   y 16-39   the frequency, large ("101.5"), "MHZ" beside it
 *   y 46-53   the level bar
 *   y 60      volume
 *   y 74-104  three lines of diagnostics (dim): how far the receiver got, interrupts, DAC; when they are empty,
 *             RDS: the station name (large, y 74) and the radio text (y 96)
 *   y 116     typed digits, a message, or the keys */
typedef struct {
	char    freq[8];                     /* "101.5" */
	bool    stereo;
	uint8_t level;                       /* 0..100 */
	uint8_t vol;                         /* 0..15 */
	char    diag[3][22];
	char    ps[9];                       /* RDS station name, "" for none */
	char    rt[22];                      /* RDS radio text, the part shown */
	char    bottom[22];
	bool    bottom_warn;
} UiFm_t;

void UI_FmDraw(const UiFm_t *f, bool full);

#endif
