/* Open-UV5R-Mini: the apps (MENU -> APPS), after the ones in F4HWN's UV-K5 firmware. Each is its own file and its own
 * switch in features.mk (Sean, 2026-11-24: every app can be left out of the build):
 *
 *   ENABLE_APP_SPECTRUM  app/spectrum.c  the band around the VFO frequency, from the BK4819's RSSI
 *   ENABLE_APP_METER     app/meter.c     signal meter for fox hunting: level, S-units, peak, history, tick sound
 *   ENABLE_APP_CHANNELS  app/chbook.c    the channel list with names; MENU picks one
 *   ENABLE_APP_SYSINFO   app/sysinfo.c   battery, calibration, flash, channels, TX lock, uptime, build
 *   ENABLE_APP_SNAKE     app/snake.c     a snake game
 *
 * app.c opens an app from the menu (or an = key action), hands it every key and calls its tick every APP_TICK_MS
 * while it is open. PTT closes it first. Drawing goes through ui/screen_apps.c (or the menu screen). */
#ifndef APP_APPS_H
#define APP_APPS_H

#include <stdbool.h>
#include <stdint.h>
#include "driver/keyboard.h"

#ifndef APP_SPECTRUM_ENABLE
#define APP_SPECTRUM_ENABLE 1
#endif
#ifndef APP_METER_ENABLE
#define APP_METER_ENABLE 1
#endif
#ifndef APP_CHANNELS_ENABLE
#define APP_CHANNELS_ENABLE 1
#endif
#ifndef APP_SYSINFO_ENABLE
#define APP_SYSINFO_ENABLE 1
#endif
#ifndef APP_SNAKE_ENABLE
#define APP_SNAKE_ENABLE 1
#endif

typedef enum {
	APPS_STAY = 0,                       /* still open */
	APPS_EXIT,                           /* back to the menu */
	APPS_TUNE,                           /* back to the main screen, the VFO tuned to gAppsTune */
	APPS_CHANNEL,                        /* back to the main screen on channel gAppsChannel (0-based) */
} AppsResult_t;

typedef struct {
	const char  *name;                   /* the menu row */
	void         (*enter)(void);
	AppsResult_t (*key)(KEY_Code_t k);   /* a key press */
	AppsResult_t (*tick)(bool full);     /* every APP_TICK_MS; full = draw everything */
	void         (*leave)(void);
} AppDef_t;

/* Set by app.c for the apps to read */
extern bool     gAppsFlashOk;            /* the external flash answered (channels can be read) */
extern uint32_t gAppsUptimeMs;           /* since power-on */
/* Set by an app before it returns APPS_TUNE / APPS_CHANNEL */
extern uint32_t gAppsTune;
extern int      gAppsChannel;

#if APP_SPECTRUM_ENABLE
extern const AppDef_t APP_SPECTRUM;
#endif
#if APP_METER_ENABLE
extern const AppDef_t APP_METER;
#endif
#if APP_CHANNELS_ENABLE
extern const AppDef_t APP_CHANNELS;
#endif
#if APP_SYSINFO_ENABLE
extern const AppDef_t APP_SYSINFO;
#endif
#if APP_SNAKE_ENABLE
extern const AppDef_t APP_SNAKE;
#endif

/* "145.520" (MHz, 3 decimals) from 10 Hz units; returns the end */
char *APPS_FormatMhz(char *b, uint32_t f);
/* decimal, no padding, a '-' for negatives; returns the end */
char *APPS_PutInt(char *b, int v);

#endif
