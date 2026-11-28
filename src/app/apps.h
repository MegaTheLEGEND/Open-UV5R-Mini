/* Open-UV5R-Mini: the apps (MENU -> APPS), after the ones in F4HWN's UV-K5 firmware. Each is its own file and its own
 * switch in features.mk (Sean, 2026-11-24: every app can be left out of the build):
 *
 *   ENABLE_APP_SPECTRUM  app/spectrum.c  the band around the VFO frequency, from the BK4819's RSSI
 *   ENABLE_APP_METER     app/meter.c     signal meter for fox hunting: level, S-units, peak, history, tick sound
 *   ENABLE_APP_CHANNELS  app/chbook.c    the channel list with names; MENU picks one
 *   ENABLE_APP_SYSINFO   app/sysinfo.c   battery, calibration, flash, channels, TX lock, uptime, build (opened
 *                                        from MENU -> ABOUT with 1, not listed under APPS)
 *   ENABLE_APP_APRS      app/aprs.c      APRS: heard stations, our position beacon
 *   ENABLE_APP_SUBGHZ    app/subghz.c    315/433 MHz remote controls, receive only
 *   ENABLE_APP_SNAKE     app/snake.c     a snake game
 *   ENABLE_APP_FM        app/fmradio.c   FM broadcast radio (needs JieLi's libraries, features.mk)
 *   ENABLE_APP_BTSCAN    app/btscan.c    BLE devices nearby (needs a Bluetooth backend that scans; bt/bt.h)
 *   ENABLE_APP_BTINFO    app/btinfo.c    what the Bluetooth backend can do, the radio's BT address, BTIF
 *   ENABLE_APP_BTBEACON  app/btbeacon.c  the radio's own BLE beacon: name, iBeacon or Eddystone URL
 *   ENABLE_APP_BTSERIAL  app/btserial.c  a terminal on the Bluetooth serial link
 *   ENABLE_APP_BTCHIRP   app/btchirp.c   CHIRP programming over the Bluetooth serial link
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
#ifndef APP_APRS_ENABLE
#define APP_APRS_ENABLE 1
#endif
#ifndef APP_SUBGHZ_ENABLE
#define APP_SUBGHZ_ENABLE 1
#endif
#ifndef APP_BTSCAN_ENABLE
#define APP_BTSCAN_ENABLE 1
#endif
#ifndef APP_BTINFO_ENABLE
#define APP_BTINFO_ENABLE 1
#endif
#ifndef APP_BTBEACON_ENABLE
#define APP_BTBEACON_ENABLE 1
#endif
#ifndef APP_BTSERIAL_ENABLE
#define APP_BTSERIAL_ENABLE 1
#endif
#ifndef APP_BTCHIRP_ENABLE
#define APP_BTCHIRP_ENABLE 1
#endif
#ifndef APP_FM_ENABLE
#define APP_FM_ENABLE 0
#endif

typedef enum {
	APPS_STAY = 0,                       /* still open */
	APPS_EXIT,                           /* back to the menu */
	APPS_TUNE,                           /* back to the main screen, the VFO tuned to gAppsTune */
	APPS_CHANNEL,                        /* back to the main screen on channel gAppsChannel (0-based) */
	APPS_RELOAD,                         /* stay open; the channels and settings changed (BT CHIRP): load them again */
} AppsResult_t;

typedef struct {
	const char  *name;                   /* the menu row */
	void         (*enter)(void);
	AppsResult_t (*key)(KEY_Code_t k);   /* a key press */
	AppsResult_t (*tick)(bool full);     /* every APP_TICK_MS; full = draw everything */
	void         (*leave)(void);
	bool         ptt_tunes;              /* PTT transmits on gAppsTune (the spectrum's centre), on the selected side */
	bool         ptt_stays;              /* PTT transmits on the selected side and the app stays open */
	const char  *help;                   /* the key help page (lamp held): lines of up to 21 characters, "\n" between */
} AppDef_t;

/* app.c's services for the apps: the selected side, changed as the main screen's keys would (channel mode with the
 * channel's tones and shift, or the VFO), so PTT transmits on what the app shows */
bool        APP_ChannelMode(void);
int         APP_Channel(void);              /* 0-based, -1 in VFO mode */
const char *APP_ChannelName(void);          /* "" if none */
uint32_t    APP_FmKhz(void);                /* the FM radio's saved station, kHz; 0 = none */
uint8_t     APP_FmVolume(void);             /* its saved volume 0-15; above 15 = none */
void        APP_SetFm(uint32_t khz, uint8_t vol);   /* saved once it stays the same a few seconds */
void        APP_AprsConf(uint8_t out[10]);   /* the APRS settings, packed (radio/aprs.h) */
void        APP_SetAprsConf(const uint8_t in[10]);
void        APP_HoldSaves(bool on);         /* the app has the VFO on its own frequency: don't save it meanwhile */
void        APP_SetChannelMode(bool mr);
void        APP_Step(int dir);              /* next / previous channel, or one frequency step */
bool        APP_TuneChannel(int n);         /* false: an empty slot */
bool        APP_TuneFreq(uint32_t f);       /* VFO mode; false: outside 18-660 MHz */
const char *APP_TxRefused(void);            /* why the last PTT was refused, or 0 */

/* Set by app.c for the apps to read */
extern bool     gAppsFlashOk;            /* the external flash answered (channels can be read) */
extern uint32_t gAppsUptimeMs;           /* since power-on */
/* Set by an app before it returns APPS_TUNE / APPS_CHANNEL (and kept current by a ptt_tunes app) */
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
/* features.mk ENABLE_DEBUGGING: bring-up aids (FM debug lines, cable monitor, cable log); crash readouts stay */
#ifndef DEBUGGING
#define DEBUGGING 0
#endif
/* with DEBUGGING built in, MENU -> ABOUT switches it on and off while the radio runs (kept with the settings; on until first set) */
extern bool gDebug;
#define DEBUG_ON (DEBUGGING && gDebug)
#if APP_FM_ENABLE
extern const AppDef_t APP_FM;
/* Development: power on straight into the FM radio (receiver started), with the cable monitor waiting for the PC
 * (platform/jl_sdk/fmrx.c), so the FM bring-up needs no key presses; a crash restarts the radio into it again. */
#ifndef FM_DEV_AUTOSTART
#define FM_DEV_AUTOSTART 0
#endif
#endif
#if APP_APRS_ENABLE
extern const AppDef_t APP_APRS;
#endif
#if APP_SUBGHZ_ENABLE
extern const AppDef_t APP_SUBGHZ;
#endif
#if APP_SNAKE_ENABLE
extern const AppDef_t APP_SNAKE;
#endif
#if APP_BTSCAN_ENABLE
extern const AppDef_t APP_BTSCAN;
#endif
#if APP_BTINFO_ENABLE
extern const AppDef_t APP_BTINFO;
#endif
#if APP_BTBEACON_ENABLE
extern const AppDef_t APP_BTBEACON;
#endif
#if APP_BTSERIAL_ENABLE
extern const AppDef_t APP_BTSERIAL;
#endif
#if APP_BTCHIRP_ENABLE
extern const AppDef_t APP_BTCHIRP;
#endif

/* "145.520" (MHz, 3 decimals) from 10 Hz units; returns the end */
char *APPS_FormatMhz(char *b, uint32_t f);
/* decimal, no padding, a '-' for negatives; returns the end */
char *APPS_PutInt(char *b, int v);

#endif
