/* Open-UV5R-Mini: the radio application: main screen, frequency entry, menu, PTT.
 *
 * Laid out the way the UV-K5 open firmware splits a radio (a radio core in radio/radio.c, screens drawn from state,
 * keys turned into actions), written fresh for this radio. Not tried on a radio yet. The cable update mode is kept:
 * PTT-A + '=' at power-on, as stock (update/update_radio.c).
 */
#include <stdbool.h>
#include <stdint.h>
#include "app/app.h"
#include "app/battery.h"
#include "app/apps.h"
#include "store/calib.h"
#include "store/channels.h"
#include "app/keys.h"
#if KEYTEST_ENABLE
#include "app/keytest.h"
#endif
#include "app/monitor.h"
#include "driver/uart.h"
#include "app/program.h"
#include "app/version.h"
#include "radio/radio.h"
#include "radio/dualwatch.h"
#include "radio/dtmfseq.h"
#include "radio/scan.h"
#include "radio/vox.h"
#include "store/settings.h"
#include "radio/tones.h"
#include "radio/txlock.h"
#include "update/update.h"
#include "board.h"
#include "bt/bt.h"
#if BT_LE_ENABLE
#include "platform/jl_sdk/bt_le.h"
#endif
#include "driver/adc.h"
#include "driver/bk4819.h"
#include "driver/extflash.h"
#include "driver/gpio.h"
#include "driver/io.h"
#include "driver/irq.h"
#include "driver/keyboard.h"
#include "driver/st7735s.h"
#include "driver/system.h"
#include "driver/uart.h"
#include "ui/helper.h"
#include "ui/screen_main.h"
#include "ui/screen_menu.h"
#include "ui/theme.h"
#include "ui/status.h"
#if APP_FM_ENABLE
#include "platform/jl_sdk/fmrx.h"
#endif


typedef enum { SCREEN_MAIN, SCREEN_MENU, SCREEN_ABOUT, SCREEN_APP, SCREEN_HELP } Screen_t;

static Screen_t s_screen;
static bool     s_redraw = true;         /* whole screen */

/* Something shown changed. The main screen draws only what differs from what it shows (UI_MainDraw), so it needs no
 * full redraw, which wiped the whole screen for a lamp icon or each dual-watch hop (Sean, 2026-11-27); the menus
 * draw everything again. */
static void changed(void)
{
	if (s_screen != SCREEN_MAIN)
		s_redraw = true;
}
#define LAMP_STROBE_MS 500u          /* strobe half-period: a guess, the stock rate is not read yet */
static uint8_t  s_lamp_mode;             /* 0 off, 1 on, 2 strobe (stock flashlight key) */
static uint16_t s_lamp_ms;
/* The function key (#, as the UV-K5's F; Sean, 2026-11-27): a tap switches F on and off, shown at the left of every
 * top bar. On the main screen F then a key does what holding that key does, and F goes off; in an app F then UP / DOWN
 * stand for # and * (the apps' second pair). F is for one key, then off, on every screen (Sean, 2026-11-27). */
static bool     s_fkey;
static char     s_entry[7];              /* digits typed, MHz then kHz */
static uint8_t  s_entry_len;
static uint8_t  s_menu_sel, s_menu_edit;
uint16_t gAppTickMs = 10;               /* app/app.h APP_TICK_MS */
static uint16_t s_off_ticks, s_batt_ticks;   /* in ms */
static uint32_t s_light_ms, s_tx_ms;     /* backlight on for, transmitting for */
static uint32_t s_idle_ms;                /* main screen with no key, for auto lock */
static uint32_t s_menu_ms;                /* menu with no key, for the menu time-out */
static bool     s_light_on = true, s_tot_hit;
static bool     s_mr;                    /* channel mode */
static int      s_ch = -1;               /* channel slot shown, 0-based */
static char     s_ch_name[13];
static bool     s_xf_ok;                 /* external flash answered */
static bool     s_ptt_down, s_tx_refused;
static KEY_Code_t s_key_held;           /* the key down now (key_event; defined there) */
static uint32_t s_trx_ms;                /* how long the radio has been transmitting, or receiving with squelch open */
static uint8_t  s_tot_left_s;            /* seconds before the TX time-out ends this transmission, in the last 10 */
static bool     s_lock_ptt;              /* the key lock also locks PTT (MENU -> LOCK PTT) */
static bool     s_boot_eq;               /* switched on with = held */
static bool     s_duty_on;               /* the receiver's duty cycle is running (battery save or sleep) */
static uint8_t  s_batt_show;             /* 0 icon, 1 percent, 2 volts; saved in gStock.ext[EXT_BATT] */
static const char *s_tx_why;              /* what the bottom line says while PTT is refused */
static const char *s_note;              /* a message on the bottom line for a few seconds (saving, loading) */
static uint16_t s_note_color;
static uint32_t s_note_ms;

static void note(const char *text, uint16_t color)
{
	s_note = text;
	s_note_color = color;
	s_note_ms = 4000u;
}
static uint16_t s_tail_ms;     /* transmit still on after PTT was let go */
static bool     s_eot;         /* the down code is being sent after PTT was let go */

/* PTT-ID: the up (bit 0) or down (bit 1) code if the channel (in VFO mode setting 0x0B) and the ANI block both ask
 * for it (stock 0x01E507B0 / 0x01E508C2: channel byte 13 bit, then the ANI byte 6 bit). Stock does something else when the channel asks and
 * the ANI byte doesn't (a group call path, not followed here). */
static bool ptt_id_start(uint8_t bit)
{
	const uint8_t *codes = bit == 1 ? gStock.up_code : gStock.down_code;

	uint8_t mode = s_mr ? gVfo.ptt_id : gStock.raw[SET_PTT_ID];   /* stock sub_01E3F9D0 */

	if (!(mode & bit) || !(gStock.ani[6] & bit))
		return false;
	return DTMFSEQ_Start(codes, bit == 1 ? DTMFSEQ_WaitMs(gStock.raw[0x0C]) : 0, DTMFSEQ_TimeMs(gStock.ani[7]),
	                     DTMFSEQ_TimeMs(gStock.ani[8]));
}

/* transmit end, the stock order: roger, tail tone, then 400 ms more (sub_01E4DCC2) */
static void tx_release(void)
{
	if (gStock.raw[SET_ROGER])
		RADIO_Roger();
	if (gStock.raw[SET_STE])
		RADIO_TxTail();
	s_tail_ms = RADIO_TX_TAIL_MS;
}

static void ptt_id_tick(void)
{
	int c = DTMFSEQ_Tick(APP_TICK_MS);

	if (c >= 0)
		RADIO_TxTone((uint8_t)c, (gStock.raw[SET_DTMF_MON] & 2u) != 0);
	else if (c == DTMFSEQ_GAP)
		RADIO_TxToneSilence();
	else if (c == DTMFSEQ_DONE) {
		RADIO_TxToneOff();
		if (s_eot) {
			s_eot = false;
			tx_release();
		}
	}
}

/* The two sides, A and B, as stock has them. gVfo, s_mr, s_ch and s_ch_name are the active side's working copy;
 * s_side[] holds both and is brought up to date before the other side is tuned. */
typedef struct {
	Vfo_t vfo;
	bool  mr;
	int   ch;
	char  name[13];
} Side_t;
static Side_t   s_side[2];
static uint8_t  s_active;                /* 0 = A, 1 = B */
static bool     s_peek;                  /* dual watch: tuned to the other side for a moment */
static bool     s_peek_held;             /* ... and it opened */
static void side_save(void);

static char *put_num(char *p, unsigned n)          /* decimal, no padding; returns the end */
{
	char t[6];
	int i = 0;

	do
		t[i++] = (char)('0' + n % 10u);
	while ((n /= 10u) && i < 6);
	while (i)
		*p++ = t[--i];
	return p;
}
static bool     s_tscan;                 /* tone scan running */
static uint16_t s_tscan_last, s_tscan_ms;
static uint8_t  s_tscan_same;
static char     s_dtmf[17];              /* DTMF heard on receive */
static uint8_t  s_dtmf_len;
static uint32_t s_dtmf_ms;
static void     tscan_start(void);
static void side_switch(void);
static void dw_end(void);

static char *fmt_step(char *b, uint16_t s)        /* 10 Hz units -> "12.5K", "6.25K", "100K" */
{
	char *p = b, t[6];
	uint32_t whole = s / 100u, frac = s % 100u;
	int n = 0;

	do
		t[n++] = (char)('0' + whole % 10);
	while ((whole /= 10) && n < 5);
	while (n)
		*p++ = t[--n];
	if (frac) {
		*p++ = '.';
		*p++ = (char)('0' + frac / 10);
		if (frac % 10)
			*p++ = (char)('0' + frac % 10);
	}
	*p++ = 'K';
	*p = 0;
	return b;
}

/* DTMF heard: shown above the key help for the stock hang time (setting 0x39: min(n, 7) + 3 s), up to 16 characters
 * as the stock buffer. */
static void dtmf_tick(void)
{
	char c = RADIO_DtmfPoll();
	uint8_t hang = gStock.raw[0x39] > 7 ? 7 : gStock.raw[0x39];

	if (c) {
		if (s_dtmf_len >= 16)
			s_dtmf_len = 0;
		s_dtmf[s_dtmf_len++] = c;
		s_dtmf[s_dtmf_len] = 0;
		s_dtmf_ms = 0;
	} else if (s_dtmf_len && (s_dtmf_ms += APP_TICK_MS) >= (hang + 3u) * 1000u) {
		s_dtmf_len = 0;
		s_dtmf[0] = 0;
	}
}

/* ------------------------------------------------------------------------------------------- main screen
 * Both sides at once (src/ui/screen_main.h): this fills the screen's snapshot from the radio state, and the screen
 * draws what changed. */
static UiMain_t s_view;
static int8_t   s_meter_kept = -1;       /* the active side's meter while dual watch looks at the other side */

/* RSSI to 0..7 bars: about -125 dBm and up, 9 dB a bar. Only while there is a signal (the chip's squelch result,
 * RADIO_Carrier): on a radio (2026-11-24) the noise floor alone lit 2-3 bars, which read as a signal. */
static int8_t meter_bars(void)
{
	int dbm = (int)RADIO_Rssi() / 2 - 160, n = (dbm + 125) / 9;

	if (!RADIO_Carrier())
		return 0;
	return (int8_t)(n < 0 ? 0 : n > UI_METER_BARS ? UI_METER_BARS : n);
}

static void copy_text(char *d, const char *s, unsigned n)
{
	unsigned i = 0;

	for (; i + 1 < n && s[i]; i++)
		d[i] = s[i];
	d[i] = 0;
}

static void side_view(UiSide_t *u, uint8_t n, const Vfo_t *vf, bool mr, int ch, const char *name)
{
	char b[8], t[8];
	ToneKind_t rk = TONE_Kind(vf->rx_tone), tk = TONE_Kind(vf->tx_tone);
	uint8_t dm = gStock.raw[n ? SET_DISP_B : SET_DISP_A];   /* stock display mode: 0 name, 1 frequency, 2 number */

	u->freq = vf->freq;
	u->label[0] = u->big[0] = 0;
	if (mr && ch >= 0) {
		char *p = u->label;
		const char *d = UI_FormatDec(b, (uint32_t)ch + 1, 3);
		*p++ = 'C'; *p++ = 'H'; *p++ = '-';
		while (*d)
			*p++ = *d++;
		*p++ = ' ';
		copy_text(p, name, (unsigned)(sizeof u->label - (unsigned)(p - u->label)));
		if (dm == 2)
			copy_text(u->big, u->label, 7);
		else if (dm == 0 && name[0]) {
			/* the name is the big text, so the small line gives the number and frequency: "005 145.520" (MHz,
			 * trailing zeros dropped down to 3 decimals; 12 characters at most) */
			uint32_t f = vf->freq, frac = f % 100000u;
			char *q = u->label;
			const char *d2 = UI_FormatDec(b, (uint32_t)ch + 1, 3);
			int dec = 5;
			while (*d2)
				*q++ = *d2++;
			*q++ = ' ';
			for (const char *m = put_num(t, (unsigned)(f / 100000u)), *s = t; s < m; s++)
				*q++ = *s;
			*q++ = '.';
			while (dec > 3 && frac % 10u == 0) {
				frac /= 10u;
				dec--;
			}
			for (int i = dec - 1; i >= 0; i--) {
				uint32_t p10 = 1;
				for (int j = 0; j < i; j++)
					p10 *= 10u;
				if (q < u->label + sizeof u->label - 1)
					*q++ = (char)('0' + frac / p10 % 10u);
			}
			*q = 0;
			copy_text(u->big, name, sizeof u->big);
		}
	}
	u->am = vf->mod == MOD_AM || (vf->mod == MOD_AUTO && vf->freq >= 10800000u && vf->freq < 13600000u);
	u->narrow = vf->narrow || u->am;
	u->high_power = vf->power == POWER_HIGH;
	u->shift = (int8_t)(!vf->tx_freq || vf->tx_freq == vf->freq ? 0 : vf->tx_freq > vf->freq ? 1 : -1);
	u->tone_kind = UI_TONE_NONE;
	u->tone[0] = 0;
	if (rk != TONE_NONE || tk != TONE_NONE) {
		ToneKind_t k = rk != TONE_NONE ? rk : tk;
		u->tone_kind = k == TONE_CTCSS ? UI_TONE_CTCSS : UI_TONE_DCS;
		if (rk != TONE_NONE && tk != TONE_NONE && vf->rx_tone != vf->tx_tone) {
			char *p = u->tone;
			const char *q = TONE_Format(t, vf->rx_tone);
			while (*q)
				*p++ = *q++;
			*p++ = '/';
			copy_text(p, TONE_Format(t, vf->tx_tone), (unsigned)(sizeof u->tone - (unsigned)(p - u->tone)));
		} else
			copy_text(u->tone, TONE_Format(t, rk != TONE_NONE ? vf->rx_tone : vf->tx_tone), sizeof u->tone);
	}
	u->state = UI_STATE_IDLE;
	u->meter = -1;
}

/* gUiStatus (ui/status.h): F and the battery, for every screen's top bar */
static void status_fill(void)
{
	UiStatus_t *v = &gUiStatus;
	int lvl = BATTERY_Level();

	v->fkey = s_fkey;
	v->battery = (int8_t)(lvl == 0xFF || !BATTERY_Raw() ? -1 : (lvl * 4 + 5) / 6);
	v->battery_low = BATTERY_Low();
	v->battery_hide = v->battery_low && s_batt_ticks % 1000u < 500u;
	v->battery_text[0] = 0;
	if (s_batt_show && BATTERY_Raw()) {            /* "84%" or "4.1V" (K5 feature 4.8) */
		char *p = v->battery_text;
		if (s_batt_show == 1) {
			p = put_num(p, BATTERY_Percent());
			*p++ = '%';
		} else {
			uint16_t mv = (uint16_t)(BATTERY_Millivolts() + 50u);
			*p++ = (char)('0' + mv / 1000u % 10u);
			*p++ = '.';
			*p++ = (char)('0' + mv / 100u % 10u);
			*p++ = 'V';
		}
		*p = 0;
	}
}

static void main_view(UiMain_t *v)
{
	uint8_t other = (uint8_t)(s_active ^ 1u), tuned = (uint8_t)(s_active ^ (s_peek ? 1u : 0u));
	const Side_t *o = &s_side[other];
	RadioState_t st = RADIO_State();
	char b[12];

	/* while dual watch looks at the other side, gVfo is that side's; the active side is in s_side[] */
	side_view(&v->side[s_active], s_active, s_peek ? &s_side[s_active].vfo : &gVfo, s_mr, s_ch, s_ch_name);
	side_view(&v->side[other], other, &o->vfo, o->mr, o->ch, o->name);
	v->active = s_active;
	if (!s_peek || s_peek_held) {
		UiSide_t *u = &v->side[tuned];
		u->state = st == RADIO_TX ? UI_STATE_TX : st == RADIO_RX_OPEN ? UI_STATE_RX : UI_STATE_IDLE;
		u->meter = st == RADIO_TX ? UI_METER_BARS : meter_bars();
		if (!s_peek)
			s_meter_kept = u->meter;
	} else
		v->side[s_active].meter = s_meter_kept;   /* a 100 ms look at the other side: keep the meter still */

	v->entry[0] = 0;
	if (s_entry_len) {
		unsigned i = 0;
		if (s_mr) {
			v->entry[i++] = 'C'; v->entry[i++] = 'H'; v->entry[i++] = ' ';
			for (int k = 0; k < 3; k++)
				v->entry[i++] = k < s_entry_len ? s_entry[k] : '-';
		} else
			for (int k = 0; k < 6; k++) {
				if (k == 3)
					v->entry[i++] = '.';
				v->entry[i++] = k < s_entry_len ? s_entry[k] : '-';
			}
		while (i < 9)
			v->entry[i++] = ' ';
		v->entry[i] = 0;
	}

	v->squelch = gVfo.squelch;
	v->dual_watch = gStock.raw[SET_DUAL_WATCH] != 0;
	v->scan = SCAN_Active() || SCAN_Holding();
	v->scan_hold = SCAN_Holding();
	v->lamp = s_lamp_mode != 0;
	v->strobe = s_lamp_mode == 2;
	v->warning = !CALIB_Present();
	v->locked = gStock.raw[SET_KEY_LOCK] != 0;
	v->fkey = gUiStatus.fkey;            /* status_fill */
	v->battery = gUiStatus.battery;
	v->battery_low = gUiStatus.battery_low;
	v->battery_hide = gUiStatus.battery_hide;
	copy_text(v->battery_text, gUiStatus.battery_text, sizeof v->battery_text);

	/* bottom line, most important first */
	v->bottom_color = COLOR_DIM;
	if (s_tx_refused) {
		copy_text(v->bottom, s_tx_why ? s_tx_why : "NO TX HERE", sizeof v->bottom);
		v->bottom_color = COLOR_TX;
	} else if (s_note_ms && s_note) {
		copy_text(v->bottom, s_note, sizeof v->bottom);
		v->bottom_color = s_note_color;
	} else if (RADIO_State() == RADIO_TX && RADIO_TxToneOn() && s_key_held == KEY_EQUALS) {
		copy_text(v->bottom, "1750 HZ", sizeof v->bottom);
		v->bottom_color = COLOR_WARN;
	} else if (RADIO_MonitorOn() && RADIO_State() != RADIO_TX) {
		copy_text(v->bottom, "MONITOR", sizeof v->bottom);
		v->bottom_color = COLOR_RX;
	} else if (gVfo.rx_tone && !s_tscan && RADIO_State() != RADIO_TX && RADIO_Carrier()) {
		/* a signal on a channel with a receive tone: say whether its tone matches (open) or not (stays muted) */
		bool ok = RADIO_ToneHeard();
		copy_text(v->bottom, ok ? "TONE OK" : "NO TONE MATCH", sizeof v->bottom);
		v->bottom_color = ok ? COLOR_ACCENT : COLOR_WARN;
	} else if (s_tscan) {
		char *p = v->bottom;
		const char *q = "TONE SCAN ";
		while (*q)
			*p++ = *q++;
		copy_text(p, s_tscan_last ? TONE_Format(b, s_tscan_last) : "...", 8);
		v->bottom_color = COLOR_WARN;
	} else if (RADIO_State() == RADIO_TX && s_tot_left_s) {
		char *p = v->bottom;
		const char *q = "TX ENDS IN ";
		while (*q)
			*p++ = *q++;
		p = put_num(p, s_tot_left_s);
		*p++ = 'S';
		*p = 0;
		v->bottom_color = COLOR_TX;
	} else if ((RADIO_State() == RADIO_TX || RADIO_State() == RADIO_RX_OPEN) && s_trx_ms >= 1000u && !s_dtmf_len) {
		/* how long this transmission or reception has gone on: "TX 0:42" */
		uint32_t t = s_trx_ms / 1000u;
		char *p = v->bottom;
		*p++ = RADIO_State() == RADIO_TX ? 'T' : 'R';
		*p++ = 'X';
		*p++ = ' ';
		p = put_num(p, (unsigned)(t / 60u));
		*p++ = ':';
		*p++ = (char)('0' + t % 60u / 10u);
		*p++ = (char)('0' + t % 10u);
		*p = 0;
		v->bottom_color = RADIO_State() == RADIO_TX ? COLOR_TX : COLOR_RX;
	} else if (s_dtmf_len) {
		char *p = v->bottom;
		*p++ = 'D'; *p++ = 'T'; *p++ = 'M'; *p++ = 'F'; *p++ = ' ';
		copy_text(p, s_dtmf, 17);
		v->bottom_color = COLOR_WARN;
	} else if (gStock.raw[0x3A] == 1u) {
		/* VOX keys the radio on any sound (a key beep, a breath): always in sight while on (Sean, 2026-11-27) */
		copy_text(v->bottom, "VOX ON", sizeof v->bottom);
		v->bottom_color = COLOR_WARN;
	} else
		v->bottom[0] = 0;               /* only DTMF and notifications here (Sean, 2026-11-24): no hints */
}

static void draw_main(void)
{
	main_view(&s_view);
	UI_MainDraw(&s_view, s_redraw);
}

/* ------------------------------------------------------------------------------------------- menu */
enum { M_AB, M_DW, M_MEM, M_SCAN, M_SCANMODE, M_SQL, M_VOL, M_STEP, M_BW, M_MOD, M_RXTONE, M_TXTONE, M_SHIFT,
       M_OFFSET, M_POWER, M_TSCAN, M_TSCANSAVE, M_CHSAVE, M_CHDEL, M_SCANADD, M_CHNAME, M_FREQ, M_SET0 };
static const char *const MENU_NAMES[M_SET0] = {
	"A/B", "DUAL WATCH", "MEMORY", "SCAN", "SCAN MODE", "SQUELCH", "VOLUME", "STEP", "BANDWIDTH", "MODULATION",
	"RX TONE", "TX TONE", "SHIFT", "OFFSET", "TX POWER", "TONE SCAN", "TSCAN SAVE", "SAVE CH", "DEL CH", "SCAN ADD",
	"CH NAME", "FREQUENCY",
};

/* Stock setting bytes with a menu row each. Values as stock and CHIRP's UV-5R Mini driver have them (settings_obj in
 * chirp/drivers/baofeng_uv17Pro.py): a list of names, or a number shown as add + mul x value with a unit. */
typedef struct {
	const char        *name;
	uint8_t            off, count;
	const char *const *list;      /* names, or 0 for a number */
	uint16_t           mul, add;
	const char        *unit;
	bool               zero_off;  /* 0 shows OFF */
} SetItem_t;
static const char *const L_ONOFF[] = { "OFF", "ON" };
static const char *const L_BACKLIGHT[] = { "ALWAYS", "5S", "10S", "15S", "20S" };
static const char *const L_PTTID[] = { "OFF", "BOT", "EOT", "BOTH" };
static const char *const L_SIDETONE[] = { "OFF", "KEYS", "ID", "BOTH" };
static const char *const L_MENU_EXIT[] = { "5S", "10S", "15S", "20S", "25S", "30S", "35S", "40S", "45S", "50S",
                                           "60S" };
static const SetItem_t SET_ITEMS[] = {
	{ "BACKLIGHT",  SET_BACKLIGHT, 5,  L_BACKLIGHT, 0,   0,   0,    false },
	{ "BEEP",       SET_BEEP,      2,  L_ONOFF,     0,   0,   0,    false },
	{ "TX TIMEOUT", SET_TOT,       13, 0,           15,  0,   "S",  true },
	{ "BUSY LOCK",  SET_BCL,       2,  L_ONOFF,     0,   0,   0,    false },
	{ "ROGER",      SET_ROGER,     2,  L_ONOFF,     0,   0,   0,    false },
	{ "TAIL ELIM",  SET_STE,       2,  L_ONOFF,     0,   0,   0,    false },
	{ "RPT TAIL",   SET_RPT_CLEAR, 11, 0,           100, 0,   "MS", false },
	{ "PTT ID",     SET_PTT_ID,    4,  L_PTTID,     0,   0,   0,    false },
	{ "SIDETONE",   SET_DTMF_MON,  4,  L_SIDETONE,  0,   0,   0,    false },
	{ "VOX",        0x3A,          2,  L_ONOFF,     0,   0,   0,    false },
	{ "VOX LEVEL",  0x02,          9,  0,           1,   1,   "",   false },
	{ "VOX DELAY",  0x20,          16, 0,           100, 500, "MS", false },
	{ "DTMF HOLD",  0x39,          8,  0,           1,   3,   "S",  false },
	{ "AUTO LOCK",  SET_AUTO_LOCK, 2,  L_ONOFF,     0,   0,   0,    false },
	{ "MENU EXIT",  0x21,          11, L_MENU_EXIT, 0,   0,   0,    false },
};
#define SET_COUNT  ((int)(sizeof SET_ITEMS / sizeof SET_ITEMS[0]))
#define M_SETEND   (M_SET0 + SET_COUNT)  /* first item after the setting bytes */
/* Items are numbered by position (draw_menu), so one left out of the build leaves no gap in the numbers. */
#define M_BATT     M_SETEND             /* battery on the status bar: icon, percent or volts (our own byte) */
#define M_SLEEP    (M_BATT + 1)         /* sleep after this long idle (our own byte) */
#define M_SLEEPNOW (M_BATT + 2)         /* sleep at once */
#define M_BSAVE    (M_BATT + 3)         /* battery save: the receiver's duty cycle while awake (our own byte) */
#define M_EQ_PRESS (M_BATT + 4)         /* what the = key does: one press, two presses, held (our own bytes) */
#define M_EQ_DBL   (M_BATT + 5)
#define M_EQ_HOLD  (M_BATT + 6)
#define M_LOCKPTT  (M_BATT + 7)         /* the key lock locks PTT too */
#if KEYTEST_ENABLE
#define M_KEYTEST  (M_LOCKPTT + 1)
#define M_ABOUT    (M_KEYTEST + 1)
#else
#define M_ABOUT    (M_LOCKPTT + 1)      /* no KEY TEST in this build (features.mk ENABLE_KEYTEST) */
#endif

/* = key actions (K5 features 1.13 monitor, 4.18 assignable keys). MONITOR held: open while = is held; MONITOR on a
 * press: on until pressed again. While transmitting, = held always sends 1750 Hz. */
enum { KA_NONE, KA_MONITOR, KA_SCAN, KA_AB, KA_VM, KA_BW, KA_POWER, KA_LAMP, KA_DW, KA_LOCK, KA_SPECTRUM, KA_METER,
       KA_SQL_UP, KA_SQL_DOWN, KA_COUNT };
static const char *const L_KEYACT[KA_COUNT] = { "NONE", "MONITOR", "SCAN", "A/B", "VFO/MR", "W/N", "POWER",
                                                "LAMP", "DUAL WATCH", "KEY LOCK", "SPECTRUM", "SIG METER",
                                                "SQUELCH +", "SQUELCH -" };
static uint8_t s_eq_act[3] = { KA_NONE, KA_NONE, KA_MONITOR };   /* press, double press, hold */
static const char *const L_BATT[] = { "ICON", "PERCENT", "VOLTS" };
/* Sleep, after F4HWN's (SetOff; Sean, 2026-11-24): after this long with no key, signal or transmit (sleep_tick). */
static const char *const L_SLEEP[] = { "OFF", "1 MIN", "5 MIN", "10 MIN", "15 MIN", "30 MIN", "1 HOUR",
                                       "2 HOURS" };
static const uint16_t SLEEP_S[] = { 0, 60, 300, 600, 900, 1800, 3600, 7200 };
#define SLEEP_N       8
#define SLEEP_DEFAULT 6                  /* 1 hour */
/* Battery save (stock's Save Mode, setting 0x01, which CHIRP's UV-5R Mini driver shows as Off/On; the ratios as
 * the UV-K5's BatSav): after 10 s idle on the main screen the receiver listens 100 ms, then is off for n x 100 ms.
 * The ratio is our own byte; the stock byte is kept as on/off. */
static const char *const L_BSAVE[] = { "OFF", "1:1", "1:2", "1:4" };
static const uint8_t BSAVE_OFF[] = { 0, 1, 2, 4 };       /* off time in 100 ms */
static uint8_t s_bsave = 3;              /* 1:4; from the stock byte when ours is not set */
static bool    s_sleep_now;              /* SLEEP NOW chosen: sleep once the keys are let go */
static uint8_t s_sleep_set = SLEEP_DEFAULT;
#define M_NORMAL   (M_ABOUT + 1)         /* the normal menu: items 1..M_NORMAL */
/* Hidden items, after the normal ones: shown only when the radio was switched on with = held (the update mode's key
 * without PTT-A), which opens the menu at the first of them. They go again when the menu is left. */
#define M_REGION   M_NORMAL
#define M_TXLOCK   (M_NORMAL + 1)
#define M_COUNT    (M_NORMAL + 2)

static bool s_menu_hidden;               /* the hidden items are in the list */

/* Sections (Sean, 2026-11-24): the first page lists them, with ABOUT at the bottom; items are numbered on from one
 * section to the next, so a number typed on any page goes straight to its item. CHANNEL holds what a channel keeps
 * (SAVE CH writes it), and squelch. The hidden items (REGION, TX LOCK) are the last of SYSTEM. */
#define SI(n) (M_SET0 + (n))             /* SET_ITEMS row n */
static const uint8_t SEC_CH[] = { M_FREQ, M_CHNAME, M_RXTONE, M_TXTONE, M_SHIFT, M_OFFSET, M_POWER, M_BW, M_MOD,
                                  M_STEP, M_SQL, M_SCANADD, M_CHSAVE, M_CHDEL };
static const uint8_t SEC_SCAN[] = { M_AB, M_MEM, M_DW, M_SCAN, M_SCANMODE, M_TSCAN, M_TSCANSAVE };
static const uint8_t SEC_TX[] = { SI(2), SI(3), SI(4), SI(5), SI(6), SI(7), SI(8), SI(9), SI(10), SI(11), SI(12) };
static const uint8_t SEC_SYS[] = { M_VOL, SI(1), SI(0), M_BATT, M_BSAVE, M_SLEEP, M_SLEEPNOW, SI(13), M_LOCKPTT,
                                   SI(14), M_EQ_PRESS, M_EQ_DBL, M_EQ_HOLD,
#if KEYTEST_ENABLE
                                   M_KEYTEST,
#endif
                                   M_REGION, M_TXLOCK };
#define HIDDEN_N 2                       /* the hidden items at the end of SEC_SYS */

/* APPS and BLUETOOTH: one row per app built in (features.mk ENABLE_APP_*); menu ids M_APP0 + n, the radio apps first,
 * then the Bluetooth ones, which have their own top-level section (Sean, 2026-11-27) */
#define RAPPS_COUNT (APP_SPECTRUM_ENABLE + APP_METER_ENABLE + APP_CHANNELS_ENABLE + APP_SYSINFO_ENABLE + APP_SNAKE_ENABLE \
                     + APP_FM_ENABLE + APP_APRS_ENABLE + APP_SUBGHZ_ENABLE)
#define BTAPPS_COUNT (APP_BTSCAN_ENABLE + APP_BTINFO_ENABLE + APP_BTBEACON_ENABLE + APP_BTSERIAL_ENABLE \
                      + APP_BTCHIRP_ENABLE)
#define APPS_COUNT (RAPPS_COUNT + BTAPPS_COUNT)
#define M_APP0     M_COUNT
#if APPS_COUNT
static const AppDef_t *const APPS[APPS_COUNT] = {
#if APP_FM_ENABLE
	&APP_FM,
#endif
#if APP_SPECTRUM_ENABLE
	&APP_SPECTRUM,
#endif
#if APP_METER_ENABLE
	&APP_METER,
#endif
#if APP_CHANNELS_ENABLE
	&APP_CHANNELS,
#endif
#if APP_APRS_ENABLE
	&APP_APRS,
#endif
#if APP_SUBGHZ_ENABLE
	&APP_SUBGHZ,
#endif
#if APP_SYSINFO_ENABLE
	&APP_SYSINFO,
#endif
#if APP_SNAKE_ENABLE
	&APP_SNAKE,
#endif
	/* BLUETOOTH */
#if APP_BTSCAN_ENABLE
	&APP_BTSCAN,
#endif
#if APP_BTBEACON_ENABLE
	&APP_BTBEACON,
#endif
#if APP_BTSERIAL_ENABLE
	&APP_BTSERIAL,
#endif
#if APP_BTCHIRP_ENABLE
	&APP_BTCHIRP,
#endif
#if APP_BTINFO_ENABLE
	&APP_BTINFO,
#endif
};
#endif
typedef struct {
	const char    *name;
	const uint8_t *items;                /* the menu ids, or 0: apps M_APP0 + app0 + row */
	uint8_t        n;
	uint8_t        app0;
} Section_t;
static const Section_t SECTIONS[] = {
	{ "CHANNEL",  SEC_CH,   sizeof SEC_CH, 0 },
	{ "SCAN",     SEC_SCAN, sizeof SEC_SCAN, 0 },
	{ "TRANSMIT", SEC_TX,   sizeof SEC_TX, 0 },
#if RAPPS_COUNT
	{ "APPS",      0, RAPPS_COUNT, 0 },
#endif
#if BTAPPS_COUNT
	{ "BLUETOOTH", 0, BTAPPS_COUNT, RAPPS_COUNT },
#endif
	{ "SYSTEM",   SEC_SYS,  sizeof SEC_SYS, 0 },     /* last: the hidden items are at its end */
};
#define SEC_COUNT ((int)(sizeof SECTIONS / sizeof SECTIONS[0]))
static int8_t  s_menu_sec = -1;          /* the section open; -1 = the first page */
static uint8_t s_menu_page_sel;          /* the first page's selected row (SEC_COUNT = ABOUT) */
static uint8_t s_menu_num, s_menu_num_len;   /* an item number being typed */

/* row r of section sec: its menu id */
static uint8_t sec_item(int sec, int r)
{
	const Section_t *t = &SECTIONS[sec];
	return t->items ? t->items[r] : (uint8_t)(M_APP0 + t->app0 + r);
}

static int sec_len(int sec)
{
	return SECTIONS[sec].n - (sec == SEC_COUNT - 1 && !s_menu_hidden ? HIDDEN_N : 0);
}

static int sec_first_num(int sec)
{
	int n = 1;
	for (int i = 0; i < sec; i++)
		n += sec_len(i);
	return n;
}

static int menu_total(void)
{
	return sec_first_num(SEC_COUNT) - 1;
}

static int sec_row(int sec, int id)              /* the row of item id in sec, or 0 */
{
	for (int r = 0; r < SECTIONS[sec].n; r++)
		if (sec_item(sec, r) == id)
			return r;
	return 0;
}


static const char *menu_name(int i)
{
	if (i < M_SET0)
		return MENU_NAMES[i];
	if (i < M_SETEND)
		return SET_ITEMS[i - M_SET0].name;
#if APPS_COUNT
	if (i >= M_APP0 && i < M_APP0 + APPS_COUNT)
		return APPS[i - M_APP0]->name;
#endif
	switch (i) {
#if KEYTEST_ENABLE
	case M_KEYTEST: return "KEY TEST";
#endif
	case M_BATT:    return "BATTERY";
	case M_SLEEP:   return "SLEEP";
	case M_SLEEPNOW: return "SLEEP NOW";
	case M_LOCKPTT: return "LOCK PTT";
	case M_BSAVE:   return "BAT SAVE";
	case M_EQ_PRESS: return "= PRESS";
	case M_EQ_DBL:  return "= DOUBLE";
	case M_EQ_HOLD: return "= HOLD";
	case M_ABOUT:   return "ABOUT";
	case M_REGION:  return "REGION";
	case M_TXLOCK:  return "TX LOCK";
	default:        return "";
	}
}

static const char *set_value(const SetItem_t *it, char *b)
{
	uint8_t v = gStock.raw[it->off] < it->count ? gStock.raw[it->off] : 0;
	char *p;

	if (it->list)
		return it->list[v];
	if (it->zero_off && !v)
		return "OFF";
	p = put_num(b, (unsigned)(it->add + it->mul * v));
	copy_text(p, it->unit, 4);
	return b;
}

/* Repeater shift: the TX frequency follows the RX one at a fixed offset. s_offset keeps the offset while the shift
 * is off, so turning it back on finds it again. */
static uint32_t s_offset;                /* 10 Hz */
static int      s_ch_pick = -1;          /* SAVE CH: the slot being chosen, 0-based */
static bool     s_ch_typed;              /* a digit was typed in SAVE CH or OFFSET since the row was opened */

static int vfo_shift(void)
{
	if (gVfo.rx_only || !gVfo.tx_freq || gVfo.tx_freq == gVfo.freq)
		return 0;
	return gVfo.tx_freq > gVfo.freq ? 1 : -1;
}

static uint32_t vfo_offset(void)
{
	int sh = vfo_shift();
	return sh > 0 ? gVfo.tx_freq - gVfo.freq : sh < 0 ? gVfo.freq - gVfo.tx_freq : s_offset;
}

static void shift_set(int dir)
{
	if (!s_offset)
		s_offset = gVfo.freq < 30000000u ? 60000u : 500000u;    /* 600 kHz on 2 m, 5 MHz on 70 cm */
	gVfo.rx_only = false;
	if (dir > 0)
		gVfo.tx_freq = gVfo.freq + s_offset;
	else if (dir < 0 && gVfo.freq > s_offset)
		gVfo.tx_freq = gVfo.freq - s_offset;
	else
		gVfo.tx_freq = 0;
}

/* a new RX frequency keeps the shift */
static void vfo_retune(uint32_t f)
{
	int sh = vfo_shift();

	s_offset = vfo_offset();
	gVfo.freq = f;
	if (sh)
		shift_set(sh);
}

/* CH NAME editing: the name padded to 12, the cursor, and the characters UP/DOWN step through */
static char    s_name_buf[13];
static int8_t  s_name_cur;
static const char NAME_CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-./+#*()";
/* FREQUENCY typed in the menu: kHz digits so far */
static uint32_t s_freq_khz;
static uint8_t  s_freq_n;

static const char *menu_value(int i, char *b)
{
	static const char *const MOD[] = { "AUTO", "FM", "AM" };

	if (i >= M_SET0 && i < M_SETEND)
		return set_value(&SET_ITEMS[i - M_SET0], b);
	if (i >= M_APP0 && i < M_APP0 + APPS_COUNT)
		return "OPEN";
	switch (i) {
	case M_AB:      return s_active ? "B" : "A";
	case M_DW:      return gStock.raw[SET_DUAL_WATCH] ? "ON" : "OFF";
	case M_MEM:     return s_mr ? "CHANNELS" : "VFO";
	case M_SCAN:    return "START";
	case M_TSCAN:   return "START";
	case M_TSCANSAVE: {
		static const char *const TS[] = { "RX+TX", "RX", "TX" };
		return TS[gStock.raw[0x2B] <= 2 ? gStock.raw[0x2B] : 0];
	}
	case M_SCANMODE: {
		static const char *const SM[] = { "TIME", "CARRIER", "SEARCH" };
		return SM[gStock.raw[SET_SCAN_MODE] <= 2 ? gStock.raw[SET_SCAN_MODE] : 1];
	}
	case M_SQL:     return UI_FormatDec(b, gVfo.squelch, 1);
	case M_VOL:     return UI_FormatDec(b, gVfo.volume, 2);
	case M_STEP:    return fmt_step(b, RADIO_Steps[gVfo.step]);
	case M_BW:      return gVfo.narrow ? "NARROW" : "WIDE";
	case M_MOD:     return MOD[gVfo.mod];
	case M_RXTONE:  return TONE_Format(b, gVfo.rx_tone);
	case M_TXTONE:  return TONE_Format(b, gVfo.tx_tone);
	case M_SHIFT:   return gVfo.rx_only ? "RX ONLY" : vfo_shift() > 0 ? "+" : vfo_shift() < 0 ? "-" : "OFF";
	case M_OFFSET: {                     /* MHz.kHz */
		uint32_t o = vfo_offset();
		char *p = put_num(b, (unsigned)(o / 100000u));
		o = (o / 100u) % 1000u;
		*p++ = '.';
		*p++ = (char)('0' + o / 100u);
		*p++ = (char)('0' + o / 10u % 10u);
		*p++ = (char)('0' + o % 10u);
		*p = 0;
		return b;
	}
	case M_POWER:   return gVfo.power == POWER_LOW ? "LOW" : "HIGH";
#if SETTINGS_ENABLE_SAVE
	case M_CHSAVE: {
		Channel_t c;
		int n = s_menu_edit ? s_ch_pick : (s_mr ? s_ch : -1);
		char *p;
		if (n < 0)
			return s_xf_ok ? "PICK" : "NO FLASH";
		p = put_num(b, (unsigned)n + 1u);
		if (s_menu_edit)
			copy_text(p, CHANNEL_Load((uint16_t)n, &c) ? " USED" : " NEW", 6);
		else
			*p = 0;
		return b;
	}
	case M_CHDEL: {
		char *p;
		if (!s_mr || s_ch < 0)
			return "-";
		p = put_num(b, (unsigned)s_ch + 1u);
		if (s_menu_edit)
			copy_text(p, " SURE?", 7);
		else
			*p = 0;
		return b;
	}
	case M_SCANADD: {
		Channel_t c;
		if (!s_mr || s_ch < 0 || !CHANNEL_Load((uint16_t)s_ch, &c))
			return "-";
		return c.scan ? "ON" : "OFF";
	}
#else
	case M_CHSAVE: case M_CHDEL: case M_SCANADD: return "NO SAVE";
#endif
	case M_BATT:    return L_BATT[s_batt_show < 3 ? s_batt_show : 0];
	case M_SLEEP:   return L_SLEEP[s_sleep_set < SLEEP_N ? s_sleep_set : SLEEP_DEFAULT];
	case M_SLEEPNOW: return "PRESS MENU";
	case M_LOCKPTT: return s_lock_ptt ? "ON" : "OFF";
	case M_BSAVE:   return L_BSAVE[s_bsave < 4 ? s_bsave : 0];
	case M_CHNAME:
		if (s_menu_edit)
			return s_name_buf;
		if (!s_mr || s_ch < 0)
			return "VFO: NONE";
		return s_ch_name[0] ? s_ch_name : "NO NAME";
	case M_FREQ:
		if (s_menu_edit && s_freq_n) {       /* "145.5__" while typing */
			uint32_t v = s_freq_khz;
			for (int k = 0; k < 7; k++)
				b[k] = k == 3 ? '.' : '_';
			b[7] = 0;
			for (int k = s_freq_n - 1; k >= 0; k--) {   /* the digits typed, from the last one back */
				b[k < 3 ? k : k + 1] = (char)('0' + v % 10u);
				v /= 10u;
			}
			return b;
		}
		return UI_FormatFreq(b, gVfo.freq);
	case M_EQ_PRESS: case M_EQ_DBL: case M_EQ_HOLD:
		return L_KEYACT[s_eq_act[i - M_EQ_PRESS] < KA_COUNT ? s_eq_act[i - M_EQ_PRESS] : 0];
	case M_ABOUT:   return FW_VERSION;
	case M_REGION:  return TXLOCK_RegionName(TXLOCK_Region());
	case M_TXLOCK:  return TXLOCK_ModeName(TXLOCK_Mode(), TXLOCK_Region());
	default:        return "";
	}
}

static void channel_show(int n);

#if SETTINGS_ENABLE_SAVE
/* the working VFO as a channel record (the name kept when the slot had one) */
static int channel_write(int n)
{
	Channel_t c, old;
	bool had = CHANNEL_Load((uint16_t)n, &old);

	for (int i = 0; i < 13; i++)
		c.name[i] = had ? old.name[i] : 0;
	c.rx = gVfo.freq;
	c.tx = gVfo.rx_only ? 0 : gVfo.tx_freq ? gVfo.tx_freq : gVfo.freq;
	c.rx_tone = gVfo.rx_tone;
	c.tx_tone = gVfo.tx_tone;
	c.low_power = gVfo.power == POWER_LOW;
	c.narrow = gVfo.narrow;
	c.scan = had ? old.scan : true;
	c.ptt_id = gVfo.ptt_id;
	return CHANNEL_Save((uint16_t)n, &c);
}

static int first_free_channel(void)
{
	Channel_t c;
	for (int n = 0; n < (int)CHANNEL_COUNT; n++)
		if (!CHANNEL_Load((uint16_t)n, &c))
			return n;
	return 0;
}
#endif

static void menu_change(int i, int dir)
{
	if (i >= M_SET0 && i < M_SETEND) {
		const SetItem_t *it = &SET_ITEMS[i - M_SET0];
		uint8_t v = gStock.raw[it->off] < it->count ? gStock.raw[it->off] : 0;
		gStock.raw[it->off] = (uint8_t)((v + it->count + dir) % it->count);
		if (it->off == 0x3A)
			VOX_Reset();
		return;
	}
	switch (i) {
	case M_BATT:
		s_batt_show = (uint8_t)((s_batt_show + 3 + dir) % 3);
		return;
	case M_SLEEP:
		s_sleep_set = (uint8_t)((s_sleep_set + SLEEP_N + dir) % SLEEP_N);
		return;
	case M_BSAVE:
		s_bsave = (uint8_t)((s_bsave + 4 + dir) % 4);
		return;
	case M_LOCKPTT:
		s_lock_ptt = !s_lock_ptt;
		return;
	case M_CHNAME: {                     /* the character at the cursor, through NAME_CHARS */
		int n = (int)sizeof NAME_CHARS - 1, c = 0;
		while (c < n && NAME_CHARS[c] != s_name_buf[s_name_cur])
			c++;
		s_name_buf[s_name_cur] = NAME_CHARS[((c < n ? c : 0) + n + dir) % n];
		return;
	}
	case M_FREQ: {                       /* one step, on the step grid */
		uint32_t st = RADIO_Steps[gVfo.step], f = gVfo.freq - gVfo.freq % st;
		f = dir > 0 ? f + st : f - st;
		s_freq_n = 0;
		if (f >= FREQ_MIN && f <= FREQ_MAX)
			vfo_retune(f);
		break;
	}
	case M_EQ_PRESS: case M_EQ_DBL: case M_EQ_HOLD: {
		uint8_t *a = &s_eq_act[i - M_EQ_PRESS];
		*a = (uint8_t)((*a + KA_COUNT + (dir ? dir : 1)) % KA_COUNT);
		RADIO_Monitor(false);            /* a monitor left on by the old action ends */
		return;
	}
	case M_REGION:                       /* fixed by the build: no change */
		if (TXLOCK_RegionFixed())
			RADIO_Beep(400, 120);
		else
			TXLOCK_Set(TXLOCK_Mode(), (Region_t)((TXLOCK_Region() + REGION_COUNT + dir) % REGION_COUNT));
		return;
	case M_TXLOCK: {                     /* the next mode this build and region have */
		int m = TXLOCK_Mode();
		do
			m = (m + TXL_COUNT + (dir ? dir : 1)) % TXL_COUNT;
		while (!TXLOCK_Available((TxLock_t)m, TXLOCK_Region()));
		TXLOCK_Set((TxLock_t)m, TXLOCK_Region());
		return;
	}
	case M_MEM:
		s_mr = s_xf_ok && !s_mr;
		if (s_mr)
			channel_show(CHANNEL_Next(s_ch < 0 ? -1 : s_ch - 1, +1));
		else {
			gVfo.tx_freq = 0;
			gVfo.rx_only = false;
		}
		break;
	case M_AB:
		side_switch();
		return;
	case M_DW:
		gStock.raw[SET_DUAL_WATCH] = !gStock.raw[SET_DUAL_WATCH];
		break;
	case M_TSCANSAVE:
		gStock.raw[0x2B] = (uint8_t)(((gStock.raw[0x2B] <= 2 ? gStock.raw[0x2B] : 0) + 3 + dir) % 3);
		break;
	case M_SCANMODE:
		gStock.raw[SET_SCAN_MODE] = (uint8_t)(((gStock.raw[SET_SCAN_MODE] <= 2 ? gStock.raw[SET_SCAN_MODE] : 1)
		                                       + 3 + dir) % 3);
		break;
	case M_SQL:   gVfo.squelch = (uint8_t)((gVfo.squelch + 10 + dir) % 10); break;
	case M_VOL:   gVfo.volume = (uint8_t)((gVfo.volume + 16 + dir) % 16); break;
	case M_STEP:  gVfo.step = (uint8_t)((gVfo.step + RADIO_StepCount + dir) % RADIO_StepCount); break;
	case M_BW:    gVfo.narrow = !gVfo.narrow; break;
	case M_MOD:   gVfo.mod = (Modulation_t)((gVfo.mod + 3 + dir) % 3); break;
	case M_RXTONE: gVfo.rx_tone = TONE_Step(gVfo.rx_tone, dir); break;
	case M_TXTONE: gVfo.tx_tone = TONE_Step(gVfo.tx_tone, dir); break;
	case M_SHIFT: {                      /* OFF, +, - */
		int sh = vfo_shift();
		int next = dir > 0 ? (sh == 0 ? 1 : sh == 1 ? -1 : 0) : (sh == 0 ? -1 : sh == -1 ? 1 : 0);
		s_offset = vfo_offset();
		gVfo.rx_only = false;
		if (next)
			shift_set(next);
		else
			gVfo.tx_freq = 0;
		break;
	}
	case M_OFFSET: {                     /* 100 kHz a step */
		int sh = vfo_shift();
		s_offset = vfo_offset();
		if (dir > 0 && s_offset < 10000000u - 10000u)
			s_offset += 10000u;
		else if (dir < 0)
			s_offset = s_offset > 10000u ? s_offset - 10000u : 0;
		if (sh)
			shift_set(sh);
		break;
	}
	case M_POWER: gVfo.power = gVfo.power == POWER_LOW ? POWER_HIGH : POWER_LOW; break;
#if SETTINGS_ENABLE_SAVE
	case M_CHSAVE:
		if (s_ch_pick < 0)
			s_ch_pick = 0;
		s_ch_pick = (s_ch_pick + (int)CHANNEL_COUNT + dir) % (int)CHANNEL_COUNT;
		return;
#endif
	default: break;
	}
	RADIO_Apply();
}

static void draw_menu(void)
{
	UiMenu_t m;
	char b[14];
	static char rng[SEC_COUNT][8];
	char *p;

	m.edit = s_menu_edit != 0;
	m.cursor = -1;
	m.corner_warn = s_menu_num_len != 0;
	for (int r = 0; r < UI_MENU_ROWS; r++) {
		m.name[r] = m.right[r] = 0;
		m.num[r] = 0;
		m.hidden[r] = false;
	}
	if (s_menu_sec < 0) {                /* the first page: the sections with their number ranges, then ABOUT */
		m.title = "MENU";
		m.count = (uint8_t)(SEC_COUNT + 1);
		m.sel = s_menu_page_sel;
		for (int i = 0; i < SEC_COUNT; i++) {
			int a = sec_first_num(i);
			p = put_num(rng[i], (unsigned)a);
			*p++ = '-';
			put_num(p, (unsigned)(a + sec_len(i) - 1));
		}
		m.top = UI_MenuTop(m.sel);
		for (int r = 0; r < UI_MENU_ROWS && m.top + r < m.count; r++) {
			int i = m.top + r;
			m.name[r] = i < SEC_COUNT ? SECTIONS[i].name : "ABOUT";
			m.right[r] = i < SEC_COUNT ? rng[i] : 0;
		}
		if (m.sel < SEC_COUNT) {             /* "14 ITEMS" */
			copy_text(put_num(b, (unsigned)sec_len(m.sel)), " ITEMS", 7);
			m.value = b;
		} else
			m.value = FW_VERSION;
	} else {
		const Section_t *sec = &SECTIONS[s_menu_sec];
		int first = sec_first_num(s_menu_sec);
		m.title = sec->name;
		m.count = (uint8_t)sec_len(s_menu_sec);
		m.sel = (uint8_t)sec_row(s_menu_sec, s_menu_sel);
		m.top = UI_MenuTop(m.sel);
		for (int r = 0; r < UI_MENU_ROWS && m.top + r < m.count; r++) {
			int id = sec_item(s_menu_sec, m.top + r);
			m.name[r] = menu_name(id);
			m.num[r] = (uint8_t)(first + m.top + r);
			m.hidden[r] = id == M_REGION || id == M_TXLOCK;
		}
		m.value = menu_value(s_menu_sel, b);
		if (s_menu_sel == M_CHNAME && s_menu_edit)
			m.cursor = s_name_cur;
		m.corner_warn |= s_menu_sel == M_REGION || s_menu_sel == M_TXLOCK;
	}
	p = m.corner;
	if (s_menu_num_len) {                /* the number being typed */
		p = put_num(p, s_menu_num);
		*p++ = '_';
		*p = 0;
	} else if (s_menu_sec >= 0) {        /* NN/MM */
		const char *q = UI_FormatDec(b, (uint32_t)(sec_first_num(s_menu_sec) + m.sel), 2);
		*p++ = q[0]; *p++ = q[1]; *p++ = '/';
		q = UI_FormatDec(b, (uint32_t)menu_total(), 2);
		*p++ = q[0]; *p++ = q[1]; *p = 0;
	} else
		*p = 0;
	UI_MenuDraw(&m, s_redraw);
}
/* ------------------------------------------------------------------------------------------- keys */
static void go(Screen_t s)
{
	if (s == SCREEN_MAIN) {
		s_menu_hidden = false;           /* the hidden items go with the menu */
		s_menu_sec = -1;                 /* the menu opens at its first page */
		s_menu_num_len = 0;
	}
	s_screen = s;
	s_redraw = true;
}

static void freq_step(int dir)
{
	uint32_t st = RADIO_Steps[gVfo.step];
	uint32_t f = gVfo.freq - gVfo.freq % st;

	if (dir > 0)
		f += st;
	else if (f == gVfo.freq)
		f -= st;
	if (f < FREQ_MIN || f > FREQ_MAX) {
		RADIO_Beep(400, 80);
		return;
	}
	vfo_retune(f);
	RADIO_Apply();
}

/* Show channel slot n (0-based): its frequencies, tones, power and width go into the VFO. */
static void channel_show(int n)
{
	Channel_t ch;

	if (n < 0 || !CHANNEL_Load((uint16_t)n, &ch)) {
		RADIO_Beep(400, 120);
		return;
	}
	s_ch = n;
	for (int i = 0; i < 13; i++)
		s_ch_name[i] = ch.name[i];
	gVfo.freq = ch.rx;
	gVfo.tx_freq = ch.tx;
	gVfo.rx_only = ch.tx == 0;
	gVfo.rx_tone = ch.rx_tone;
	gVfo.tx_tone = ch.tx_tone;
	gVfo.power = ch.low_power ? POWER_LOW : POWER_HIGH;
	gVfo.ptt_id = ch.ptt_id;
	gVfo.narrow = ch.narrow;
	RADIO_Apply();
}

static void entry_finish(void)
{
	if (s_mr) {
		uint32_t n = 0;
		for (int i = 0; i < s_entry_len; i++)
			n = n * 10 + (uint32_t)(s_entry[i] - '0');
		s_entry_len = 0;
		changed();
		channel_show(n >= 1 && n <= CHANNEL_COUNT ? (int)n - 1 : -1);
		return;
	}
	uint32_t khz = 0;

	for (int i = 0; i < 6; i++)
		khz = khz * 10 + (uint32_t)(i < s_entry_len ? s_entry[i] - '0' : 0);
	s_entry_len = 0;
	if (khz * 100u < FREQ_MIN || khz * 100u > FREQ_MAX) {
		RADIO_Beep(400, 120);
		changed();
		return;
	}
	vfo_retune(khz * 100u);
	RADIO_Apply();
	changed();
}

/* ------------------------------------------------------------------------------------------- tone scan
 * As the stock tone scan: the chip's sub-audio scan, read every 100 ms; a result read twice in a row is kept, in
 * the receive tone, the transmit tone or both by stock setting 0x2B (0 both, 1 receive, 2 transmit). */
static void tscan_start(void)
{
	SCAN_Stop();
	gVfo.rx_tone = 0;                    /* no tone squelch while looking */
	RADIO_Apply();
	BK4819_CssScanStart();
	s_tscan = true;
	s_tscan_last = s_tscan_ms = 0;
	s_tscan_same = 0;
	s_entry_len = 0;
}

static void tscan_stop(void)
{
	if (!s_tscan)
		return;
	s_tscan = false;
	RADIO_Apply();                       /* REG_51 back to the VFO's own tone */
}

static void tscan_tick(void)
{
	uint32_t v;
	uint16_t t = 0;

	if (!s_tscan || (s_tscan_ms += APP_TICK_MS) < 100u)
		return;
	s_tscan_ms = 0;
	switch (BK4819_CssScanRead(&v)) {
	case BK4819_TONE_CTCSS: t = TONE_FromCtcss(v); break;
	case BK4819_TONE_DCS_N: t = TONE_FromDcsWord(v); break;
	default: break;
	}
	if (!t) {
		s_tscan_same = 0;
		return;
	}
	s_tscan_same = t == s_tscan_last ? (uint8_t)(s_tscan_same + 1) : 1;
	s_tscan_last = t;
	if (s_tscan_same < 2)
		return;
	if (gStock.raw[0x2B] != 2)
		gVfo.rx_tone = t;
	if (gStock.raw[0x2B] != 1)
		gVfo.tx_tone = t;
	tscan_stop();
	RADIO_Beep(1200, 120);
}

/* ------------------------------------------------------------------------------------------- scanning */
/* The stock VFO scan range, whole MHz (setting bytes 0x2C-0x2F); stock defaults 400-470. */
static void scan_range(uint32_t *lo, uint32_t *hi)
{
	uint32_t l = (uint32_t)(gStock.raw[0x2C] | gStock.raw[0x2D] << 8), h = (uint32_t)(gStock.raw[0x2E] | gStock.raw[0x2F] << 8);

	if (!l || h <= l || h > FREQ_MAX / 100000u) {
		l = 400;
		h = 470;
	}
	*lo = l * 100000u;
	*hi = h * 100000u;
}

static void scan_hop(void)
{
	if (s_mr) {
		int n = s_ch;
		for (unsigned i = 0; i < CHANNEL_COUNT; i++) {
			Channel_t ch;
			n = CHANNEL_Next(n, SCAN_Dir());
			if (n < 0)
				break;
			if (CHANNEL_Load((uint16_t)n, &ch) && ch.scan) {
				channel_show(n);
				return;
			}
		}
		SCAN_Stop();                     /* no channel to scan */
		RADIO_Beep(400, 150);
		return;
	}
	uint32_t lo, hi, st = RADIO_Steps[gVfo.step], f;
	scan_range(&lo, &hi);
	f = gVfo.freq - gVfo.freq % st;
	f = SCAN_Dir() > 0 ? f + st : f - st;
	if (f < lo || f > hi || gVfo.freq < lo || gVfo.freq > hi)
		f = SCAN_Dir() > 0 ? lo : hi;
	vfo_retune(f);
	RADIO_Apply();
}

static uint32_t s_scan_from;             /* where the scan started: frequency, and channel (-1 in VFO mode) */
static int      s_scan_from_ch = -1;

static void scan_start(int dir)
{
	uint8_t m = gStock.raw[SET_SCAN_MODE];

	if (!SCAN_Active()) {
		s_scan_from = gVfo.freq;
		s_scan_from_ch = s_mr ? s_ch : -1;
	}

	SCAN_Start(dir, (ScanResume_t)(m <= 2 ? m : 1), APP_TICK_MS);
	s_entry_len = 0;
}

static void key_action(uint8_t a);
#if SETTINGS_ENABLE_SAVE
static void save_soon(void);
#endif
#if APP_FM_ENABLE
static void app_open(const AppDef_t *a);
#endif

/* The flashlight key, on every screen: off, on, strobe (stock 0x01E537C8: RAM 0x198C0+2831, mod 3) */
static void lamp_step(void)
{
	s_lamp_mode = (uint8_t)((s_lamp_mode + 1u) % 3u);
	s_lamp_ms = 0;
	IO_Lamp(s_lamp_mode != 0);
}

/* Held, or F then the key, on the main screen: the UV-K5's (egzumer's) layout where this radio has the same thing,
 * our nearest feature where it hasn't (K5: 1 band, 4 frequency copy, 8 reverse, 9 call channel). Returns false for
 * a key with no second job, which then does its usual one. */
static bool key_fn(KEY_Code_t k, bool held)
{
	switch (k) {
	case KEY_1: {                        /* the next tuning step */
		static char t[16] = "STEP ";
		char b[12];
		menu_change(M_STEP, +1);
		copy_text(t + 5, fmt_step(b, RADIO_Steps[gVfo.step]), sizeof t - 5u);
		note(t, COLOR_INFO);
		changed();
#if SETTINGS_ENABLE_SAVE
		save_soon();
#endif
		return true;
	}
	case KEY_2: key_action(KA_AB); return true;
	case KEY_3: key_action(KA_VM); return true;
	case KEY_4: key_action(KA_METER); return true;
	case KEY_5: key_action(KA_SPECTRUM); return true;
	case KEY_6:
		key_action(KA_POWER);
		note(gVfo.power == POWER_LOW ? "POWER LOW" : "POWER HIGH", COLOR_INFO);
		return true;
	case KEY_7:                          /* VOX on / off (stock setting 0x3A): F then 7 only, never 7 held, which a
	                                      * slow 7 typed first did by accident (Sean, 2026-11-27) */
		if (held)
			return true;
		gStock.raw[0x3A] = gStock.raw[0x3A] == 1u ? 0u : 1u;
		VOX_Reset();
		note(gStock.raw[0x3A] ? "VOX ON" : "VOX OFF", COLOR_INFO);
#if SETTINGS_ENABLE_SAVE
		save_soon();
#endif
		return true;
	case KEY_8: key_action(KA_DW); return true;
	case KEY_9:
		key_action(KA_MONITOR);
		note(RADIO_MonitorOn() ? "MONITOR ON" : "MONITOR OFF", COLOR_INFO);
		return true;
	case KEY_0:
#if APP_FM_ENABLE
		app_open(&APP_FM);
#else
		RADIO_Beep(400, 120);
#endif
		return true;
	case KEY_STAR:                       /* held: scan; F then *: the tone scan (K5: CTCSS / DCS scan) */
		if (held)
			key_action(KA_SCAN);
		else if (!s_mr)
			tscan_start();
		return true;
	case KEY_ACT_UP: key_action(KA_SQL_UP); return true;
	case KEY_ACT_DOWN: key_action(KA_SQL_DOWN); return true;
	case KEY_ACT_EXIT: return true;      /* F off, nothing else */
	default: return false;
	}
}

static void key_main(KEY_Code_t k)
{
	int d;

	if (KEY_IS_LONG(k) && !s_entry_len && !s_tscan && !SCAN_Active()) {
		key_fn(KEY_BASE(k), true);
		return;
	}
	k = KEY_BASE(k);                     /* while typing or scanning, a held key is the key */
	d = KEY_Digit(k);
	if (s_fkey && !s_entry_len && !s_tscan && !SCAN_Active()) {
		s_fkey = false;
		if (key_fn(k, false))
			return;
	}

	if (s_tscan) {                       /* any key ends the tone scan */
		tscan_stop();
		return;
	}
	if (SCAN_Active()) {                 /* any key ends the scan; UP/DOWN turn it round */
		if (k == KEY_ACT_UP || k == KEY_ACT_DOWN)
			scan_start(k == KEY_ACT_UP ? +1 : -1);
		else {
			SCAN_Stop();
			if (k == KEY_ACT_EXIT) {         /* EXIT: back where the scan started; any other key keeps the find */
				if (s_mr && s_scan_from_ch >= 0)
					channel_show(s_scan_from_ch);
				else if (!s_mr) {
					vfo_retune(s_scan_from);
					RADIO_Apply();
				}
				note("SCAN CANCELLED", COLOR_DIM);
			}
		}
		return;
	}

	if (d >= 0) {
		if (s_entry_len < 6)
			s_entry[s_entry_len++] = (char)('0' + d);
		if (s_entry_len == (s_mr ? 3 : 6))
			entry_finish();
		return;
	}
	if (s_entry_len) {
		if (k == KEY_ACT_EXIT) {
			s_entry_len = 0;
			changed();
		} else if (k == KEY_ACT_MENU)
			entry_finish();
		return;
	}
	if ((k == KEY_ACT_UP || k == KEY_ACT_DOWN) && s_mr)
		channel_show(CHANNEL_Next(s_ch, k == KEY_ACT_UP ? +1 : -1));
	else if (k == KEY_ACT_UP)
		freq_step(+1);
	else if (k == KEY_ACT_DOWN)
		freq_step(-1);
	else if (k == KEY_ACT_MENU) {
		s_menu_edit = 0;
		s_menu_sec = -1;
		go(SCREEN_MENU);
	} else if (k == KEY_ACT_BW) {
		gVfo.narrow = !gVfo.narrow;
		RADIO_Apply();
	} else if (k == KEY_ACT_LAMP)
		lamp_step();
	else if (k == KEY_ACT_AB || k == KEY_ACT_VM) {
		menu_change(k == KEY_ACT_AB ? M_AB : M_MEM, 0);
		changed();
	}
}

bool gDebug = true;                      /* app/apps.h DEBUG_ON: only used in DEBUGGING builds */

/* MENU -> ABOUT: name, version and build; any key goes back to the menu. In DEBUGGING builds MENU switches the
 * debugging aids on and off (Sean, 2026-11-27); kept with the settings (our byte 19). */
static void draw_about(void)
{
	ST7735S_FillRect(0, 0, 128, 128, COLOR_BG);
	UI_TitleBar("ABOUT", 0);
	UI_PrintString(FW_NAME, 22, 20, COLOR_ACCENT, COLOR_BG, 1);
	UI_PrintString("VERSION", 4, 38, COLOR_DIM, COLOR_BG, 1);
	UI_PrintString(FW_VERSION, 58, 38, COLOR_TEXT, COLOR_BG, 1);
	UI_PrintString("BUILD", 4, 52, COLOR_DIM, COLOR_BG, 1);
	UI_PrintString(VERSION_Build(), 58, 52, COLOR_TEXT, COLOR_BG, 1);
	UI_PrintString("RADIO", 4, 66, COLOR_DIM, COLOR_BG, 1);
	UI_PrintString(FW_TARGET, 4, 78, COLOR_TEXT, COLOR_BG, 1);
#if DEBUGGING
	UI_PrintString("DEBUG", 4, 94, COLOR_DIM, COLOR_BG, 1);
	UI_PrintString(gDebug ? "ON " : "OFF", 58, 94, gDebug ? COLOR_WARN : COLOR_TEXT, COLOR_BG, 1);
	UI_PrintString("RESCUE", 4, 105, COLOR_DIM, COLOR_BG, 1);
	UI_PrintString("*", 58, 105, COLOR_TEXT, COLOR_BG, 1);
	UI_PrintString("MENU:DEBUG  ELSE:BACK", 0, 118, COLOR_DIM, COLOR_BG, 1);
#else
	UI_PrintString("OPEN FIRMWARE", 4, 96, COLOR_DIM, COLOR_BG, 1);
	UI_PrintString("ANY KEY: BACK", 0, 118, COLOR_DIM, COLOR_BG, 1);
#endif
}

#if DEBUGGING
/* Restart into the boot ROM's USB download mode (the rescue mode), without the USB key. The AC695N's boot ROM
 * (dumped from Sean's radio, read 2026-11-27) checks 8 bytes of RAM at 0x10804 before booting: "usb" 0xED 0x63 0x81
 * 0x67 0xEF means USB download mode (another tag, "uart"..., the UART one); it clears the tag once read, so the
 * next power-on boots normally. The ROM restarts the chip itself by writing 0x10 to P33 register 0xA0 (P3_PR_PWR in
 * JieLi's public BR23 SDK). The PC must be on the radio's USB lines (the rescue cable) to see it. Not run on a
 * radio yet. */
static void reboot_to_rescue(void)
{
	IRQ_GlobalOff();
	/* Sean, 2026-11-27: the first try restarted but the PC found nothing. Our cable UART had the USB pins (D+ / D-)
	 * and the USB controller off, and the soft reset may leave those as they are: give them back, and hold off a
	 * moment so the PC sees the line let go before the ROM starts its USB device. */
	UART_Release();
	/* one * does it all (Sean, 2026-11-27: no second press, no fixed wait): the lines let go for 1 s, then the
	 * restart */
	for (unsigned i = 0; i < 1000u; i++) {
		SYSTEM_WatchdogClear();
		SYSTEM_DelayUs(1000);
	}
	*(volatile uint32_t *)0x00010804u = 0xED627375u;   /* "usb" 0xED */
	*(volatile uint32_t *)0x00010808u = 0xEF678163u;
	P33_Write(0xA0u, 0x10u);
	for (;;)
		;
}
#endif

/* ------------------------------------------------------------------------------------------- apps (app/apps.h) */
static const AppDef_t *s_app;            /* the app open (SCREEN_APP) */

/* ---- for the apps (app/apps.h): the selected side, as the main screen's keys change it */
bool        APP_ChannelMode(void) { return s_mr; }
int         APP_Channel(void) { return s_mr ? s_ch : -1; }
const char *APP_ChannelName(void) { return s_ch_name; }

void APP_SetChannelMode(bool mr)
{
	if (mr != s_mr)
		menu_change(M_MEM, 0);
	RADIO_Apply();
}

void APP_Step(int dir)                   /* the next channel, or a frequency step */
{
	if (s_mr)
		channel_show(CHANNEL_Next(s_ch, dir));
	else
		freq_step(dir);
}

bool APP_TuneChannel(int n)              /* 0-based; false if the slot is empty */
{
	if (!s_xf_ok || n < 0 || n >= (int)CHANNEL_COUNT || CHANNEL_Next(n - 1, +1) != n)
		return false;
	s_mr = true;
	channel_show(n);
	return true;
}

bool APP_TuneFreq(uint32_t f)            /* VFO mode; false outside the receiver's range */
{
	if (f < FREQ_MIN || f > FREQ_MAX)
		return false;
	if (s_mr)
		menu_change(M_MEM, 0);
	vfo_retune(f);
	RADIO_Apply();
	return true;
}

const char *APP_TxRefused(void) { return s_tx_refused ? (s_tx_why ? s_tx_why : "NO TX HERE") : 0; }
static bool s_app_from_menu;             /* opened from the menu: EXIT goes back there, else to the main screen */
static bool s_ptt_here;                  /* this PTT press transmits on the selected side (it left the spectrum) */

static void app_open(const AppDef_t *a)
{
	dw_end();
	SCAN_Stop();
	tscan_stop();
	RADIO_Monitor(false);
	s_app_from_menu = s_screen == SCREEN_MENU;
	s_app = a;
	a->enter();
	go(SCREEN_APP);
}

/* the app is done: its result says where to go */
static void app_close(AppsResult_t r)
{
	const AppDef_t *a = s_app;

	s_app = 0;
	if (a)
		a->leave();
	if (r == APPS_TUNE) {                /* the VFO on that frequency (VFO mode, no shift) */
		s_mr = false;
		s_ch = -1;
		gVfo.tx_freq = 0;
		gVfo.rx_only = false;
		vfo_retune(gAppsTune);
		RADIO_Apply();
		go(SCREEN_MAIN);
	} else if (r == APPS_CHANNEL && gAppsChannel >= 0) {
		s_mr = true;
		channel_show(gAppsChannel);
		go(SCREEN_MAIN);
	} else if (r == APPS_CHANNEL || r == APPS_EXIT)
		go(a != 0 && s_app_from_menu ? SCREEN_MENU : SCREEN_MAIN);
}

/* MENU on a row: rows that act at once, rows that ask first (SAVE CH, DEL CH), or edit with UP/DOWN. */
static void menu_enter(void)
{
#if APPS_COUNT
	if (s_menu_sel >= M_APP0 && s_menu_sel < M_APP0 + APPS_COUNT) {
		app_open(APPS[s_menu_sel - M_APP0]);
		return;
	}
#endif
	switch (s_menu_sel) {
#if KEYTEST_ENABLE
	case M_KEYTEST:
		KEYTEST_Run();
		s_redraw = true;
		return;
#endif
	case M_ABOUT:
		go(SCREEN_ABOUT);
		return;
	case M_SLEEPNOW:
		s_sleep_now = true;              /* sleep_tick sleeps once MENU is let go */
		go(SCREEN_MAIN);
		return;
	case M_CHNAME:
#if SETTINGS_ENABLE_SAVE
		if (!s_mr || s_ch < 0 || !s_xf_ok) {
			RADIO_Beep(400, 120);
			return;
		}
		if (!s_menu_edit) {                  /* start: the name padded to 12 with spaces, cursor on the first */
			int k = 0;
			for (; k < 12 && s_ch_name[k]; k++)
				s_name_buf[k] = s_ch_name[k];
			for (; k < 12; k++)
				s_name_buf[k] = ' ';
			s_name_buf[12] = 0;
			s_name_cur = 0;
			s_menu_edit = 1;
			return;
		}
		s_menu_edit = 0;                     /* MENU again: save it (trailing spaces dropped) */
		{
			Channel_t c;
			int k = 12;
			while (k > 0 && s_name_buf[k - 1] == ' ')
				k--;
			if (!CHANNEL_Load((uint16_t)s_ch, &c)) {
				note("SAVE FAILED", COLOR_TX);
				return;
			}
			for (int j = 0; j < 13; j++)
				c.name[j] = j < k ? s_name_buf[j] : 0;
			if (CHANNEL_Save((uint16_t)s_ch, &c) == 0) {
				note("NAME SAVED", COLOR_ACCENT);
				channel_show(s_ch);
			} else
				note("SAVE FAILED", COLOR_TX);
		}
#else
		RADIO_Beep(400, 120);
#endif
		return;
	case M_FREQ:
		if (s_menu_edit && s_freq_n) {       /* digits typed but not all 6: the rest are 0, as on the main screen */
			uint32_t khz = s_freq_khz;
			for (int k = s_freq_n; k < 6; k++)
				khz *= 10u;
			s_freq_n = 0;
			if (khz * 100u >= FREQ_MIN && khz * 100u <= FREQ_MAX) {
				vfo_retune(khz * 100u);
				RADIO_Apply();
			} else
				RADIO_Beep(400, 120);
		}
		s_freq_n = 0;
		s_menu_edit = !s_menu_edit;
		return;
	case M_SCAN:
		scan_start(+1);
		go(SCREEN_MAIN);
		return;
	case M_TSCAN:
		tscan_start();
		go(SCREEN_MAIN);
		return;
#if SETTINGS_ENABLE_SAVE
	case M_CHSAVE:
		if (!s_xf_ok) {
			RADIO_Beep(400, 120);
			return;
		}
		if (!s_menu_edit) {
			s_ch_pick = s_mr && s_ch >= 0 ? s_ch : first_free_channel();
			s_ch_typed = false;
			s_menu_edit = 1;
			return;
		}
		s_menu_edit = 0;
		if (channel_write(s_ch_pick) == 0) {
			note("CHANNEL SAVED", COLOR_ACCENT);
			if (s_mr && s_ch_pick == s_ch)
				channel_show(s_ch);
		} else
			note("CHANNEL SAVE FAILED", COLOR_TX);
		go(SCREEN_MAIN);
		return;
	case M_CHDEL:
		if (!s_mr || s_ch < 0) {
			RADIO_Beep(400, 120);
			return;
		}
		if (!s_menu_edit) {
			s_menu_edit = 1;
			return;
		}
		s_menu_edit = 0;
		if (CHANNEL_Delete((uint16_t)s_ch) == 0) {
			int n = CHANNEL_Next(s_ch, +1);
			note("CHANNEL DELETED", COLOR_WARN);
			if (n >= 0)
				channel_show(n);
			else {                           /* no channel left: back to VFO */
				s_mr = false;
				s_ch = -1;
				gVfo.tx_freq = 0;
				gVfo.rx_only = false;
				RADIO_Apply();
			}
		} else
			note("DELETE FAILED", COLOR_TX);
		go(SCREEN_MAIN);
		return;
	case M_SCANADD: {
		Channel_t c;
		if (!s_mr || s_ch < 0 || !CHANNEL_Load((uint16_t)s_ch, &c)) {
			RADIO_Beep(400, 120);
			return;
		}
		c.scan = !c.scan;
		if (CHANNEL_Save((uint16_t)s_ch, &c))
			note("SAVE FAILED", COLOR_TX);
		return;
	}
#else
	case M_CHSAVE: case M_CHDEL: case M_SCANADD:
		RADIO_Beep(400, 120);
		return;
#endif
	default:
		s_menu_edit = !s_menu_edit;
		s_ch_typed = false;
		return;
	}
}

static void menu_open(int sec, int id)
{
	s_menu_sec = (int8_t)sec;
	s_menu_sel = (uint8_t)id;
	s_menu_edit = 0;
	s_redraw = true;
}

/* item number n (1-based, across the sections) */
static void menu_jump(int n)
{
	for (int sec = 0; sec < SEC_COUNT; sec++) {
		int first = sec_first_num(sec);
		if (n >= first && n < first + sec_len(sec)) {
			menu_open(sec, sec_item(sec, n - first));
			return;
		}
	}
	RADIO_Beep(400, 120);
}

static void key_menu(KEY_Code_t k)
{
	int d = KEY_Digit(k);

	/* An item number typed on any page (not while a value is edited): two digits, or one and MENU */
	if (s_menu_num_len && d < 0) {
		s_menu_num_len = 0;
		if (k == KEY_ACT_MENU)
			menu_jump(s_menu_num);
		draw_menu();
		if (k == KEY_ACT_MENU || k == KEY_ACT_EXIT)
			return;
	}
	if (d >= 0 && !s_menu_edit) {
		s_menu_num = (uint8_t)(s_menu_num_len ? s_menu_num * 10 + d : d);
		s_menu_num_len++;
		if (s_menu_num_len >= 2 || s_menu_num * 10 > menu_total()) {
			s_menu_num_len = 0;
			menu_jump(s_menu_num);
		}
		draw_menu();
		return;
	}

	if (s_menu_sec < 0) {                /* the first page */
		if (k == KEY_ACT_EXIT)
			go(SCREEN_MAIN);
		else if (k == KEY_ACT_MENU) {
			if (s_menu_page_sel >= SEC_COUNT)
				go(SCREEN_ABOUT);
			else
				menu_open(s_menu_page_sel, sec_item(s_menu_page_sel, 0));
		} else if (k == KEY_ACT_UP || k == KEY_ACT_DOWN)
			s_menu_page_sel = (uint8_t)((s_menu_page_sel + SEC_COUNT + 1 + (k == KEY_ACT_UP ? -1 : 1))
			                            % (SEC_COUNT + 1));
		if (s_screen == SCREEN_MENU)
			draw_menu();
		return;
	}

	if (k == KEY_ACT_EXIT) {
		if (s_menu_edit) {
			s_menu_edit = 0;                 /* a name or typed frequency being edited is dropped */
			s_freq_n = 0;
		} else {
			s_menu_page_sel = (uint8_t)s_menu_sec;
			s_menu_sec = -1;
			s_redraw = true;
		}
	} else if (k == KEY_ACT_MENU) {
		menu_enter();
		if (s_screen != SCREEN_MENU)
			return;
	} else if (k == KEY_ACT_UP || k == KEY_ACT_DOWN) {
		int dir = k == KEY_ACT_UP ? -1 : 1;
		if (s_menu_edit)
			menu_change(s_menu_sel, -dir);
		else {
			int n = sec_len(s_menu_sec);
			int r = (sec_row(s_menu_sec, s_menu_sel) + n + dir) % n;
			s_menu_sel = sec_item(s_menu_sec, r);
		}
	} else if (s_menu_edit && s_menu_sel == M_CHNAME && (k == KEY_STAR || k == KEY_HASH || d >= 0)) {
		/* * and # move the cursor; a digit is typed in and the cursor moves on */
		if (d >= 0)
			s_name_buf[s_name_cur] = (char)('0' + d);
		if (k == KEY_STAR)
			s_name_cur = (int8_t)(s_name_cur ? s_name_cur - 1 : 11);
		else
			s_name_cur = (int8_t)((s_name_cur + 1) % 12);
	} else if (d >= 0 && s_menu_edit && s_menu_sel == M_FREQ) {
		/* MHz then kHz, 6 digits as on the main screen; the 6th tunes */
		s_freq_khz = s_freq_n ? s_freq_khz * 10u + (uint32_t)d : (uint32_t)d;
		if (++s_freq_n == 6) {
			s_freq_n = 0;
			if (s_freq_khz * 100u >= FREQ_MIN && s_freq_khz * 100u <= FREQ_MAX) {
				vfo_retune(s_freq_khz * 100u);
				RADIO_Apply();
			} else
				RADIO_Beep(400, 120);
		}
	} else if (d >= 0 && s_menu_edit && s_menu_sel == M_OFFSET) {
		/* typed in kHz: 6 0 0 = 600 kHz, 5 0 0 0 = 5 MHz */
		int sh = vfo_shift();
		uint32_t khz = s_ch_typed ? vfo_offset() / 100u : 0;
		khz = khz * 10u + (uint32_t)d;
		if (khz > 99999u)
			khz = (uint32_t)d;
		s_offset = khz * 100u;
		s_ch_typed = true;
		if (sh)
			shift_set(sh);
		RADIO_Apply();
	}
#if SETTINGS_ENABLE_SAVE
	else if (d >= 0 && s_menu_edit && s_menu_sel == M_CHSAVE) {
		/* typed channel number, 1-999 */
		int n = s_ch_typed ? s_ch_pick + 1 : 0;
		n = n * 10 + d;
		if (n > (int)CHANNEL_COUNT)
			n = d;
		s_ch_pick = n ? n - 1 : 0;
		s_ch_typed = true;
	}
#endif
	draw_menu();
}
/* press events, with repeat for UP/DOWN after 0.5 s */
static KEY_Code_t s_key_held = KEY_INVALID;
#if DEBUGGING
static KEY_Code_t s_mon_key = KEY_INVALID;
static uint16_t   s_mon_key_ms;

void APP_MonKey(KEY_Code_t k, uint16_t ms)
{
	s_mon_key = k;
	s_mon_key_ms = ms;
}
#endif
static bool       s_wait_release;

/* Most keys act when pressed. Keys with a second job held act when let go if they were let go soon enough, or as
 * KEY_LONG(key) once held KEY_LONG_MS (as on the UV-K5): the lamp (help), # (F; held: the key lock), and on the
 * main screen the digits and * (key_fn). */
static bool key_holds(KEY_Code_t k)
{
	if (k == KEY_HASH || k == KEY_ACT_LAMP)
		return true;
	return s_screen == SCREEN_MAIN && (KEY_Digit(k) >= 0 || k == KEY_STAR);
}

static KEY_Code_t key_event(void)
{
	static KEY_Code_t last = KEY_INVALID, queued = KEY_INVALID, cand = KEY_INVALID;
	static uint16_t held, rep, cand_ms;  /* ms */
	static bool pend, fired;             /* last waits for its let-go; its held job was done (or not wanted) */
	KEY_Code_t raw = KEYBOARD_Poll(), k, out = KEY_INVALID;

#if DEBUGGING
	if (s_mon_key_ms) {                      /* a key pressed from the PC (!key): held for its time */
		raw = s_mon_key;
		s_mon_key_ms = s_mon_key_ms > APP_TICK_MS ? (uint16_t)(s_mon_key_ms - APP_TICK_MS) : 0;
	}
#endif

	/* a new reading counts once it has stayed KEY_DEBOUNCE_MS (the loop polls every few ms now) */
	if (raw != cand) {
		cand = raw;
		cand_ms = 0;
	} else if (cand_ms < KEY_DEBOUNCE_MS)
		cand_ms = (uint16_t)(cand_ms + APP_TICK_MS);
	k = cand_ms >= KEY_DEBOUNCE_MS ? cand : last;

	s_key_held = k;
	if (s_wait_release) {                    /* a key held since power-on: nothing until it is let go */
		if (k == KEY_INVALID)
			s_wait_release = false;
		last = queued = KEY_INVALID;
		pend = false;
		return KEY_INVALID;
	}
	if (queued != KEY_INVALID) {             /* a press that came with another key's let-go */
		out = queued;
		queued = KEY_INVALID;
		return out;
	}
	if (k != last) {
		if (pend && !fired)
			out = last;                      /* let go soon enough: the tap */
		last = k;
		held = rep = 0;
		pend = fired = false;
		if (k != KEY_INVALID) {
			if (key_holds(k))
				pend = true;
			else if (out == KEY_INVALID)
				return k;
			else
				queued = k;
		}
		return out;
	}
	if (k == KEY_INVALID)
		return KEY_INVALID;
	held = (uint16_t)(held + APP_TICK_MS < 60000u ? held + APP_TICK_MS : 60000u);
	if (RADIO_State() == RADIO_TX)
		fired = true;                        /* a key held for its DTMF tone does nothing when let go */
	if ((k == KEY_ACT_UP || k == KEY_ACT_DOWN) && held >= 500u) {   /* repeat after 0.5 s, 10 a second */
		if ((rep = (uint16_t)(rep + APP_TICK_MS)) >= 100u) {
			rep = (uint16_t)(rep - 100u);
			return k;
		}
	}
	if (pend && !fired && k == KEY_HASH && held >= KEY_LOCK_MS) {
		fired = true;
		return KEY_ACT_LOCK;                 /* # held: lock or unlock the keys */
	}
	if (pend && !fired && k != KEY_HASH && held >= KEY_LONG_MS) {
		fired = true;
		return KEY_LONG(k);
	}
	return KEY_INVALID;
}

/* ------------------------------------------------------------------------------------------- key help */
/* The lamp key held, on any screen: what the keys do there (Sean, 2026-11-27). EXIT goes back. */
static Screen_t s_help_from;

static void help_open(void)
{
	if (s_screen != SCREEN_HELP)
		s_help_from = s_screen;
	s_screen = SCREEN_HELP;
	s_redraw = true;
}

static void help_close(void)
{
	s_screen = s_help_from;
	s_redraw = true;
}

static const char *const HELP_MAIN[] = {
	"HOLD, OR F THEN KEY:",
	"1 STEP     2 A/B",
	"3 VFO/MR   4 METER",
	"5 SPECTRUM 6 POWER",
	"7 F7:VOX   8 DUAL W",
	"9 MONITOR  0 FM RADIO",
	"* SCAN     F * TONES",
	"F UP/DN    SQUELCH",
	"TAP: # F   * W/N",
	"HOLD # KEY LOCK",
	"= AS SET IN MENU",
	0
};

static const char *const HELP_MENU[] = {
	"UP/DN   MOVE",
	"0-9     ITEM NUMBER",
	"MENU    OPEN / SET",
	"EXIT    BACK",
	"# TAP   F ON/OFF",
	"# HOLD  KEY LOCK",
	"LAMP    LIGHT",
	"LAMP HOLD  THIS PAGE",
	0
};

static void draw_help(void)
{
	const char *const *l = HELP_MAIN;
	const char *title = "MAIN";
	const char *app = 0;
	uint8_t y = 14;

	if (s_help_from == SCREEN_MENU || s_help_from == SCREEN_ABOUT) {
		l = HELP_MENU;
		title = "MENU";
	} else if (s_help_from == SCREEN_APP && s_app) {
		l = 0;
		title = s_app->name;
		app = s_app->help;
	}
	ST7735S_FillRect(0, 0, 128, 128, COLOR_BG);
	UI_TitleBar("KEYS", title);
	for (; l && *l; l++, y = (uint8_t)(y + 9))
		UI_PrintString(*l, 2, y, l == HELP_MAIN ? COLOR_INFO : COLOR_TEXT, COLOR_BG, 1);
	if (app) {                           /* the app's lines, "\n" between */
		char b[22];
		unsigned n = 0;
		for (const char *p = app;; p++) {
			if (*p == '\n' || !*p) {
				b[n] = 0;
				UI_PrintString(b, 2, y, COLOR_TEXT, COLOR_BG, 1);
				y = (uint8_t)(y + 9);
				n = 0;
				if (!*p)
					break;
			} else if (n < sizeof b - 1u)
				b[n++] = *p;
		}
	}
	if (s_help_from == SCREEN_APP) {
		UI_PrintString("# TAP   F ON/OFF", 2, y, COLOR_DIM, COLOR_BG, 1);
		UI_PrintString("LAMP    LIGHT", 2, (uint8_t)(y + 9), COLOR_DIM, COLOR_BG, 1);
	}
	UI_PrintString("EXIT: BACK", 2, 118, COLOR_DIM, COLOR_BG, 1);
}

/* ------------------------------------------------------------------------------------------- entry points */
/* Start where we (or the stock app) left off: squelch and the other settings, both sides' VFOs, the active side,
 * and channel mode for a side that was in it. Our own bytes (gStock.ext, saved with the settings) add what stock's
 * records have no place for: the channel each side was on, the volume, and each side's modulation. Without them
 * (a log written by stock), channel mode starts at the first filled channel. */
enum {
	EXT_MAGIC = 0,                   /* EXT_MAGIC_VALUE once we have saved; stock leaves it 0 */
	EXT_VOLUME = 1,                  /* 0-15 */
	EXT_CH_A = 2,                    /* 2 bytes each, little-endian, 0-998; FFFF none (A at 2, B at 4) */
	EXT_MOD_A = 6,                   /* Modulation_t (A at 6, B at 7) */
	EXT_TXLOCK = 8,                  /* TxLock_t + 1 (radio/txlock.h); 0 = not set */
	EXT_REGION = 9,                  /* Region_t + 1; 0 = not set */
	EXT_BATT = 10,                   /* bits 0-1 battery on the status bar: 0 icon, 1 percent, 2 volts; bit 4 LOCK PTT */
	EXT_EQ = 11,                     /* = key actions + 1 (press 11, double 12, hold 13); 0 = the default */
	EXT_SLEEP = 14,                  /* sleep time index + 1 (L_SLEEP); 0 = the default */
	EXT_BSAVE = 15,                  /* battery save ratio + 1 (L_BSAVE); 0 = from stock's on/off byte */
	EXT_FM = 16,                     /* the FM radio's station, 10 kHz, little-endian (16, 17); 0 = not set */
	EXT_FM_VOL = 18,                 /* the FM radio's volume + 1; 0 = not set */
	EXT_DEBUG = 19,                  /* MENU -> ABOUT's debug switch + 1 (DEBUGGING builds); 0 = not set (on) */
	EXT_APRS = 20,                   /* the APRS settings, 10 bytes (20-29, radio/aprs.h APRS_Pack); 0s = not set */
	EXT_MAGIC_VALUE = 0xA5,
};

static bool ext_ok(void)
{
	return gStock.ext[EXT_MAGIC] == EXT_MAGIC_VALUE;
}

static void start_side(uint8_t side);

/* The FM radio's station and volume, kept with the settings (Sean, 2026-11-27): saved once they have stayed the
 * same FM_SAVE_WAIT_MS (a seek or held UP changes them every tick), or at power-off. */
#define FM_SAVE_WAIT_MS 5000u
static uint16_t s_fm_10k;                /* 10 kHz units; 0 = none yet */
static uint8_t  s_fm_vol = 0xFFu;        /* 0-15; 0xFF = none yet */
static uint16_t s_fm_wait;               /* ms until saved; 0 = nothing to save */

uint32_t APP_FmKhz(void) { return (uint32_t)s_fm_10k * 10u; }
uint8_t  APP_FmVolume(void) { return s_fm_vol; }

/* The APRS settings (call sign, position, icon, beacon), kept with the settings as 10 packed bytes */
static uint8_t s_aprs[10];
static bool    s_hold_saves;             /* an app has the VFO on its own frequency: no saving until it leaves */

void APP_AprsConf(uint8_t out[10])
{
	for (int i = 0; i < 10; i++)
		out[i] = s_aprs[i];
}

void APP_SetAprsConf(const uint8_t in[10])
{
	bool same = true;
	for (int i = 0; i < 10; i++) {
		same = same && s_aprs[i] == in[i];
		s_aprs[i] = in[i];
	}
#if SETTINGS_ENABLE_SAVE
	if (!same)
		save_soon();
#endif
}

void APP_HoldSaves(bool on)
{
	s_hold_saves = on;
}

void APP_SetFm(uint32_t khz, uint8_t vol)
{
	uint16_t f = (uint16_t)(khz / 10u);

	if (f != s_fm_10k || vol != s_fm_vol) {
		s_fm_10k = f;
		s_fm_vol = vol;
		s_fm_wait = FM_SAVE_WAIT_MS;
	}
}

/* The power-on picture: external flash 0x0C0000, 128 x 128 RGB565, low byte first (stock reads these 32 KB at
 * power-on, EMULATOR.md; drawn from Sean's flash dump on the PC, 2026-11-27, it is his own picture). Only read; a
 * blank area (all 0xFF) draws nothing. */
#define SPLASH_ADDR   0x0C0000u
#define SPLASH_MIN_MS 1500u

static bool splash(void)
{
	static uint8_t b[2048];              /* 8 rows a piece */
	bool blank = true;

	EXTFLASH_Read(SPLASH_ADDR, b, sizeof b);
	for (unsigned i = 0; i < sizeof b && blank; i++)
		blank = b[i] == 0xFFu;
	if (blank)
		return false;
	/* drawn in the dark, then the backlight on, so it shows all at once (drawn in sight it built up row by row,
	 * Sean, 2026-11-27) */
	ST7735S_SetBacklight(false);
	/* a few rows at a time: the flash and the LCD share SPI1 (the LCD's pixels go out by DMA), so each read finishes
	 * before its rows are drawn and each window ends before the next read (Sean, 2026-11-27: reading inside one
	 * open window drew two white rows, then black, then the radio restarted) */
	for (uint32_t a = 0; a < 128u * 128u * 2u; a += sizeof b) {
		if (a)
			EXTFLASH_Read(SPLASH_ADDR + a, b, sizeof b);
		ST7735S_SetWindow(0, (uint8_t)(a / 256u), 128, (uint8_t)(sizeof b / 256u));   /* 256 bytes a row */
		for (unsigned i = 0; i < sizeof b; i += 2u)
			ST7735S_PushPixel((uint16_t)(b[i] | b[i + 1u] << 8));
		ST7735S_End();
		SYSTEM_WatchdogClear();
	}
	ST7735S_SetBacklight(true);
	return true;
}

static void start_from_stock(void)
{
	SETTINGS_Defaults();
	s_active = 0;
	side_save();                            /* without stock settings both sides start as the default VFO */
	s_side[1] = s_side[0];
	if (!s_xf_ok || !SETTINGS_Load())
		return;
	/* A setting byte outside its menu range shows as the menu's first value (set_value), so it acts as that too:
	 * Sean's VOX byte read OFF in the menu while the VOX code took any non-zero byte as on, and blowing on the mic
	 * keyed the radio (2026-11-27). */
	for (int i = 0; i < SET_COUNT; i++)
		if (gStock.raw[SET_ITEMS[i].off] >= SET_ITEMS[i].count)
			gStock.raw[SET_ITEMS[i].off] = 0;
	if (gStock.raw[SET_SQUELCH] <= 9)
		gVfo.squelch = gStock.raw[SET_SQUELCH];
	if (ext_ok() && gStock.ext[EXT_VOLUME] <= 15)
		gVfo.volume = gStock.ext[EXT_VOLUME];
	/* stored + 1, so the 0 of a log saved before these bytes existed means "not set"; bad values fall back to HAM */
	if (ext_ok() && gStock.ext[EXT_TXLOCK] && gStock.ext[EXT_REGION])
		TXLOCK_Set((TxLock_t)(gStock.ext[EXT_TXLOCK] - 1u), (Region_t)(gStock.ext[EXT_REGION] - 1u));
	if (ext_ok() && (gStock.ext[EXT_BATT] & 3u) <= 2)
		s_batt_show = gStock.ext[EXT_BATT] & 3u;
	s_lock_ptt = ext_ok() && (gStock.ext[EXT_BATT] & 0x10u);
	if (ext_ok() && gStock.ext[EXT_SLEEP] && gStock.ext[EXT_SLEEP] <= SLEEP_N)
		s_sleep_set = (uint8_t)(gStock.ext[EXT_SLEEP] - 1u);
	if (ext_ok()) {                      /* the FM radio's last station and volume */
		uint16_t f = (uint16_t)(gStock.ext[EXT_FM] | gStock.ext[EXT_FM + 1] << 8);
		if (f >= 6400u && f <= 10800u)
			s_fm_10k = f;
		if (gStock.ext[EXT_FM_VOL] && gStock.ext[EXT_FM_VOL] <= 16u)
			s_fm_vol = (uint8_t)(gStock.ext[EXT_FM_VOL] - 1u);
		if (DEBUGGING && gStock.ext[EXT_DEBUG] && gStock.ext[EXT_DEBUG] <= 2u)
			gDebug = gStock.ext[EXT_DEBUG] == 2u;   /* as it was left (Sean, 2026-11-27) */
		for (int i = 0; i < 10; i++)
			s_aprs[i] = gStock.ext[EXT_APRS + i];
	}
	if (ext_ok() && gStock.ext[EXT_BSAVE] && gStock.ext[EXT_BSAVE] <= 4)
		s_bsave = (uint8_t)(gStock.ext[EXT_BSAVE] - 1u);
	else
		s_bsave = gStock.raw[SET_POWER_SAVE] ? 3 : 0;
	for (int i = 0; i < 3; i++)
		if (ext_ok() && gStock.ext[EXT_EQ + i] && gStock.ext[EXT_EQ + i] <= KA_COUNT)
			s_eq_act[i] = (uint8_t)(gStock.ext[EXT_EQ + i] - 1u);
	s_side[0].vfo = s_side[1].vfo = gVfo;
	s_side[0].ch = s_side[1].ch = -1;
	for (uint8_t side = 0; side < 2; side++) {
		s_active = side;
		start_side(side);
		side_save();
	}
	s_active = gStock.raw[SET_ACTIVE_SIDE] ? 1 : 0;
	gVfo = s_side[s_active].vfo;
	s_mr = s_side[s_active].mr;
	s_ch = s_side[s_active].ch;
	for (int i = 0; i < 13; i++)
		s_ch_name[i] = s_side[s_active].name[i];
	RADIO_Apply();
}

/* One side from the stock VFO log and work mode, into the working copy. */
static void start_side(uint8_t side)
{
	gVfo = s_side[side].vfo;
	s_mr = false;
	s_ch = -1;
	s_ch_name[0] = 0;
	if (gStock.have_vfo) {
		const StockVfo_t *v = &gStock.vfo[side];
		gVfo.freq = v->freq;
		gVfo.rx_tone = v->rx_tone;
		gVfo.tx_tone = v->tx_tone;
		gVfo.power = v->power ? POWER_LOW : POWER_HIGH;
		gVfo.narrow = v->narrow;
		gVfo.step = v->step < RADIO_StepCount ? v->step : 4;
		if (v->shift && v->offset)
			gVfo.tx_freq = v->shift > 0 ? v->freq + v->offset : v->freq - v->offset;
	}
	if (ext_ok() && gStock.ext[EXT_MOD_A + side] <= MOD_AM)
		gVfo.mod = (Modulation_t)gStock.ext[EXT_MOD_A + side];
	RADIO_Apply();
	if ((gStock.raw[SET_WORK_MODE] >> (side ? 4 : 0) & 0xFu) == 1) {
		int saved = ext_ok() ? (int)(gStock.ext[EXT_CH_A + 2 * side] | gStock.ext[EXT_CH_A + 2 * side + 1] << 8) : -1;
		int n = saved >= 0 && saved < (int)CHANNEL_COUNT && CHANNEL_Next(saved - 1, +1) == saved
		        ? saved : CHANNEL_Next(-1, +1);     /* the saved channel if it is still filled, else the first */
		if (n >= 0) {
			s_mr = true;
			channel_show(n);
		}
	}
}

/* ------------------------------------------------------------------------------------------- A/B and dual watch */
static void side_save(void)
{
	Side_t *s = &s_side[s_active];

	s->vfo = gVfo;
	s->mr = s_mr;
	s->ch = s_ch;
	for (int i = 0; i < 13; i++)
		s->name[i] = s_ch_name[i];
}

static void side_load(uint8_t side)
{
	const Side_t *s = &s_side[side];
	uint8_t squelch = gVfo.squelch, volume = gVfo.volume;

	s_active = side;
	gVfo = s->vfo;
	gVfo.squelch = squelch;                 /* one squelch and volume for both sides, as stock */
	gVfo.volume = volume;
	s_mr = s->mr;
	s_ch = s->ch;
	for (int i = 0; i < 13; i++)
		s_ch_name[i] = s->name[i];
	RADIO_Apply();
	changed();
}

static void side_switch(void)
{
	dw_end();
	side_save();
	side_load(s_active ^ 1u);
}

/* Tune the other side for a moment. Only the radio settings are swapped in; the screen keeps the active side
 * until the other side opens. */
static void dw_begin(void)
{
	Vfo_t keep = gVfo;

	side_save();
	gVfo = s_side[s_active ^ 1u].vfo;
	gVfo.squelch = keep.squelch;          /* one squelch and volume for both, as stock */
	gVfo.volume = keep.volume;
	RADIO_Apply();
	s_peek = true;
	s_peek_held = false;
}

static void dw_end(void)
{
	if (!s_peek)
		return;
	s_peek = s_peek_held = false;
	DW_Reset();
	gVfo = s_side[s_active].vfo;
	RADIO_Apply();
}

static void dw_tick(bool busy)
{
	switch (DW_Tick(gStock.raw[SET_DUAL_WATCH] != 0, busy, RADIO_Carrier(), APP_TICK_MS)) {
	case DW_BEGIN:
		dw_begin();
		break;
	case DW_END:
		dw_end();
		break;
	case DW_HELD:
		s_peek_held = true;
		break;
	default:
		break;
	}
}

#if SETTINGS_ENABLE_SAVE
/* ------------------------------------------------------------------------------------------- saving
 * Only with SETTINGS_ENABLE_SAVE (store/settings.h). As soon as a change is done (the key let go, nothing typed half
 * way, no PTT or scan), the state goes back into the stock layout and both logs are written; an unchanged log is not
 * written, so held keys and repeats don't wear the flash (Sean, 2026-11-22: save at once, not 3 s later). A side in channel mode keeps its stock VFO
 * record: only the work-mode bit says it is in channel mode (the channel number is not known to us yet). */
static bool     s_save_due;

static void save_now(void)
{
	s_save_due = false;
	side_save();
	gStock.ext[EXT_MAGIC] = EXT_MAGIC_VALUE;
	gStock.ext[EXT_VOLUME] = gVfo.volume;
	gStock.ext[EXT_TXLOCK] = (uint8_t)(TXLOCK_Mode() + 1u);
	gStock.ext[EXT_REGION] = (uint8_t)(TXLOCK_Region() + 1u);
	gStock.ext[EXT_BATT] = (uint8_t)(s_batt_show | (s_lock_ptt ? 0x10u : 0));
	gStock.ext[EXT_SLEEP] = (uint8_t)(s_sleep_set + 1u);
	gStock.ext[EXT_BSAVE] = (uint8_t)(s_bsave + 1u);
	gStock.ext[EXT_FM] = (uint8_t)s_fm_10k;
	gStock.ext[EXT_FM + 1] = (uint8_t)(s_fm_10k >> 8);
	gStock.ext[EXT_FM_VOL] = s_fm_vol <= 15u ? (uint8_t)(s_fm_vol + 1u) : 0u;
	if (DEBUGGING)
		gStock.ext[EXT_DEBUG] = (uint8_t)(gDebug + 1u);
	for (int i = 0; i < 10; i++)
		gStock.ext[EXT_APRS + i] = s_aprs[i];
	s_fm_wait = 0;
	gStock.raw[SET_POWER_SAVE] = s_bsave != 0;    /* stock's Save Mode as on/off, as CHIRP shows it */
	for (int i = 0; i < 3; i++)
		gStock.ext[EXT_EQ + i] = (uint8_t)(s_eq_act[i] + 1u);
	for (int i = 0; i < 2; i++) {
		uint16_t ch = s_side[i].ch < 0 ? 0xFFFFu : (uint16_t)s_side[i].ch;
		gStock.ext[EXT_CH_A + 2 * i] = (uint8_t)ch;
		gStock.ext[EXT_CH_A + 2 * i + 1] = (uint8_t)(ch >> 8);
		gStock.ext[EXT_MOD_A + i] = (uint8_t)s_side[i].vfo.mod;
	}
	gStock.raw[SET_SQUELCH] = gVfo.squelch;
	gStock.raw[SET_ACTIVE_SIDE] = s_active;
	gStock.raw[SET_WORK_MODE] = (uint8_t)((s_side[0].mr ? 0x01u : 0u) | (s_side[1].mr ? 0x10u : 0u));
	for (int i = 0; i < 2; i++) {
		const Vfo_t *v = &s_side[i].vfo;
		StockVfo_t *sv = &gStock.vfo[i];
		if (s_side[i].mr)
			continue;
		sv->freq = v->freq;
		sv->rx_tone = v->rx_tone;
		sv->tx_tone = v->tx_tone;
		sv->power = v->power == POWER_LOW;
		sv->narrow = v->narrow;
		sv->step = v->step;
		if (v->tx_freq && v->tx_freq != v->freq) {
			sv->shift = v->tx_freq > v->freq ? 1 : -1;
			sv->offset = v->tx_freq > v->freq ? v->tx_freq - v->freq : v->freq - v->tx_freq;
		} else {
			sv->shift = 0;
			sv->offset = 0;
		}
	}
	int rs = s_xf_ok ? SETTINGS_SaveSettings() : -1;
	int rv = s_xf_ok && gStock.have_vfo ? SETTINGS_SaveVfos() : 0;
	if (rs || rv) {
		/* "SAVE FAIL S1 V0 E5 ST1C": which log failed, the flash driver's reason (extflash.h), status register */
		static char m[24];
		char *p = m, b[4];
		const char *q = "SAVE FAIL S";
		while (*q)
			*p++ = *q++;
		*p++ = rs ? '1' : '0'; *p++ = ' '; *p++ = 'V'; *p++ = rv ? '1' : '0'; *p++ = ' '; *p++ = 'E';
		*p++ = (char)('0' + (s_xf_ok ? EXTFLASH_LastError() % 10u : 9u)); *p++ = ' '; *p++ = 'S'; *p++ = 'T';
		UI_FormatHex(b, EXTFLASH_ReadStatus(), 2);
		*p++ = b[0]; *p++ = b[1]; *p = 0;
		note(m, COLOR_TX);
		RADIO_Beep(300, 400);
	} else {
		note("SAVED", COLOR_ACCENT);
		s_note_ms = 1000u;
	}
}

static void save_tick(bool busy)
{
	if (!s_save_due || busy || s_hold_saves || s_key_held != KEY_INVALID)
		return;
	save_now();
}

static void save_soon(void)
{
	s_save_due = true;
}
#endif

void APP_Init(void)
{
	BOARD_Init();
	ST7735S_KeepDark(true);             /* lit once, with the picture or the main screen (below) */
	ST7735S_Init();
	KEYBOARD_Init();
	if (UPDATE_Requested()) {
		ST7735S_SetBacklight(true);
		UPDATE_Mode();          /* PTT-A + '=' at power-on, as the stock app; does not return */
	}
#if APP_FM_ENABLE
	gRadioBeepHook = FMRX_Beep;  /* key beeps on the MCU's DAC, as stock; not the BK4819 (it blipped on the air) */
#endif
	BK4819_Init();
	/* = held at power-on without PTT-A (the update mode's check, update_radio.c, without the PTT): the menu, opened
	 * at its hidden items (region, TX lock). Read here, as UPDATE_Requested reads it: rows all low, PC2 low. */
	bool hidden_menu = !KEYBOARD_IsPttPressed() && !GPIO_CheckBit(PIN_KEY_COL0);
	s_boot_eq = hidden_menu;             /* also lets the radio transmit under 5 % battery */
#if KEYTEST_ENABLE
	/* a top-row key (wired to GND) held at power-on, PTT not held: the key test */
	if (!hidden_menu && !KEYBOARD_IsPttPressed()) {
		KEY_Code_t k0 = KEYBOARD_Poll();
		if (k0 == KEY_DIRECT0 || k0 == KEY_VM || k0 == KEY_AB || k0 == KEY_LAMP) {
			ST7735S_SetBacklight(true);
			KEYTEST_Run();
		}
	}
#endif
	EXTFLASH_Init();
	s_xf_ok = EXTFLASH_ReadId() == EXTFLASH_ID;
	gAppsFlashOk = s_xf_ok;
	if (s_xf_ok) {
		static uint8_t cal[CALIB_LEN];
		EXTFLASH_Read(CALIB_ADDR, cal, sizeof cal);
		CALIB_Set(cal);
	}
	ADC_Init();
	BATTERY_Set(ADC_Read(ADC_CH_BATTERY, 8));
	RADIO_Init();
	start_from_stock();
	{
		/* "LOAD S1 V1 X1": settings log, VFO log, our extra bytes (channel numbers, volume) found at start-up */
		static char m[] = "LOAD S0 V0 X0";
		m[6] = gStock.have_settings ? '1' : '0';
		m[9] = gStock.have_vfo ? '1' : '0';
		m[12] = ext_ok() ? '1' : '0';
		note(s_xf_ok ? m : "NO EXT FLASH", s_xf_ok ? COLOR_DIM : COLOR_TX);
	}
#if PROGRAM_ENABLE
	if (s_xf_ok)
		PROGRAM_Init();         /* CHIRP over the cable (app/program.c) */
#endif
#if PROGRAM_ENABLE && PROGRAM_BT && !BT_LE_ENABLE
	BT_Init(PROGRAM_BtReceive);  /* CHIRP over Bluetooth SPP (Bluetooth edition) */
#else
	BT_Init(0);
#endif
	/* The radio's own power-on picture while the beeps' audio starts (Sean, 2026-11-27; ROADMAP 4.8). After
	 * PROGRAM_Init, which starts the LCD again (its pin check) and so cleared a picture drawn before it. */
	bool splash_on = s_xf_ok && splash();
	uint32_t splash_t0 = SYSTEM_Ms();    /* held from when it shows */
#if APP_FM_ENABLE
	FMRX_BeepPrepare();                  /* the beeps' DAC, with everything else ready (behind the picture) */
#endif
	while (splash_on && SYSTEM_Ms() - splash_t0 < SPLASH_MIN_MS)
		SYSTEM_DelayMs(10);
	ST7735S_KeepDark(false);
	ST7735S_SetBacklight(true);          /* no picture: the light comes on with the main screen */
	go(SCREEN_MAIN);
	if (hidden_menu) {
		s_menu_hidden = true;
		s_menu_sec = SEC_COUNT - 1;          /* SYSTEM, at the first hidden item */
		s_menu_sel = M_REGION;
		s_menu_edit = 0;
		s_wait_release = true;               /* the = still held is not a press */
		go(SCREEN_MENU);
	}
#if APP_FM_ENABLE && FM_DEV_AUTOSTART
	else
		app_open(&APP_FM);
#endif
}

/* Backlight as the stock setting 0x03: 0 always on, 1-4 = 5/10/15/20 s after the last key, PTT or signal. */
static void light_wake(void)
{
	s_light_ms = 0;
	if (!s_light_on) {
		s_light_on = true;
		ST7735S_SetBacklight(true);
	}
}

static void light_tick(void)
{
	uint8_t bl = gStock.raw[SET_BACKLIGHT];

	if (!bl || bl > 4 || !s_light_on)
		return;
	if ((s_light_ms += APP_TICK_MS) >= 5000u * bl) {
		s_light_on = false;
		ST7735S_SetBacklight(false);
	}
}

/* Transmit refusals from the stock settings: busy lockout in VFO mode (0x0F) refuses PTT while a signal is
 * heard; the time-out timer (0x05, n x 15 s) ends a long transmission until PTT is let go. */
/* Under 5 %: transmit refused, unless the radio was switched on with = held (Sean, 2026-11-24) */
static bool tx_batt_locked(void)
{
	return !s_boot_eq && BATTERY_Raw() && BATTERY_Percent() < 5;
}

static bool tx_busy_locked(void)
{
	return !s_mr && gStock.raw[SET_BCL] && RADIO_State() == RADIO_RX_OPEN && gVfo.squelch;
}

static void tot_tick(void)
{
	uint8_t tot = gStock.raw[SET_TOT];

	if (RADIO_State() != RADIO_TX) {
		s_tx_ms = 0;
		return;
	}
	s_tot_left_s = 0;
	if (tot && tot <= 12 && (s_tx_ms += APP_TICK_MS) + 10000u >= 15000u * tot && s_tx_ms < 15000u * tot) {
		/* the last 10 s (K5 feature 4.23, visual): "TX ENDS 9S" and the red LED blinking */
		s_tot_left_s = (uint8_t)((15000u * tot - s_tx_ms + 999u) / 1000u);
		IO_LedTx((s_tx_ms / 250u) % 2u == 0);
	}
	if (tot && tot <= 12 && s_tx_ms >= 15000u * tot) {
		RADIO_EndTx();
		s_tot_hit = s_tx_refused = true;
		s_tx_why = "TX TIME OUT";
		RADIO_Beep(400, 300);
	}
}

/* The = key: one press, two presses (the second within EQ_DOUBLE_MS of letting go) and held (EQ_HOLD_MS), each
 * with the action chosen in the menu. Only on the main screen with the keys unlocked; a press waits EQ_DOUBLE_MS
 * for a second one only when a double-press action is set. While transmitting, = sends 1750 Hz (app_tick). */
#define EQ_HOLD_MS   500u
#define EQ_DOUBLE_MS 300u

static void key_action(uint8_t a)
{
	switch (a) {
	case KA_MONITOR: RADIO_Monitor(!RADIO_MonitorOn()); break;
	case KA_SCAN:
		if (SCAN_Active())
			SCAN_Stop();
		else
			scan_start(+1);
		break;
	case KA_AB:    key_main(KEY_ACT_AB); break;
	case KA_VM:    key_main(KEY_ACT_VM); break;
	case KA_BW:    key_main(KEY_ACT_BW); break;
	case KA_LAMP:  key_main(KEY_ACT_LAMP); break;
	case KA_POWER: menu_change(M_POWER, 0); RADIO_Apply(); break;
	case KA_DW:    menu_change(M_DW, 0); break;
	case KA_LOCK:
		gStock.raw[SET_KEY_LOCK] = 1;
		note("KEYS LOCKED", COLOR_WARN);
		break;
	case KA_SQL_UP:
	case KA_SQL_DOWN: {                  /* squelch one step, shown on the bottom line (K5 feature 4.19) */
		static char t[12] = "SQUELCH ";
		if (a == KA_SQL_UP ? gVfo.squelch < 9 : gVfo.squelch > 0)
			gVfo.squelch = (uint8_t)(a == KA_SQL_UP ? gVfo.squelch + 1u : gVfo.squelch - 1u);
		RADIO_Apply();
		t[8] = (char)('0' + gVfo.squelch);
		t[9] = 0;
		note(t, COLOR_INFO);
		break;
	}
	case KA_SPECTRUM:                    /* the app, if this build has it */
#if APP_SPECTRUM_ENABLE
		app_open(&APP_SPECTRUM);
		return;
#else
		RADIO_Beep(400, 120);
		return;
#endif
	case KA_METER:
#if APP_METER_ENABLE
		app_open(&APP_METER);
		return;
#else
		RADIO_Beep(400, 120);
		return;
#endif
	default:
		return;
	}
	if (gStock.raw[SET_BEEP])
		RADIO_Beep(1000, 40);
	light_wake();
	changed();
#if SETTINGS_ENABLE_SAVE
	save_soon();
#endif
}

static void eq_tick(bool ptt)
{
	static uint8_t  taps;
	static uint16_t ms;
	static bool     held, was, block;
	bool down = s_key_held == KEY_EQUALS && !s_wait_release && s_screen == SCREEN_MAIN && !ptt
	            && RADIO_State() != RADIO_TX && !gStock.raw[SET_KEY_LOCK] && !s_entry_len;

	/* a = still down from something else (the 1750 Hz tone, the menu, the lock) counts only after it is let go */
	if (s_key_held != KEY_EQUALS)
		block = false;
	else if (!down && !was)
		block = true;
	down = down && !block;

	if (ms < 0xFFFFu - APP_TICK_MS)
		ms += APP_TICK_MS;
	if (down != was)
		ms = 0;
	if (down && !was)
		light_wake();
	if (down && !was && !held)
		taps++;
	if (down && taps == 1 && !held && ms >= EQ_HOLD_MS) {
		held = true;
		taps = 0;
		if (s_eq_act[2] == KA_MONITOR)
			RADIO_Monitor(true);         /* open while held */
		else
			key_action(s_eq_act[2]);
	}
	if (!down && held) {
		held = false;
		if (s_eq_act[2] == KA_MONITOR)
			RADIO_Monitor(false);
	}
	if (!down && taps) {
		if (taps >= 2) {
			taps = 0;
			key_action(s_eq_act[1]);
		} else if (s_eq_act[1] == KA_NONE || ms >= EQ_DOUBLE_MS) {
			taps = 0;
			key_action(s_eq_act[0]);
		}
	}
	was = down;
}

/* ------------------------------------------------------------------------------------------- power saving
 * One receiver duty cycle for both, as on the UV-K5 (its app/app.c, FUNCTION_POWER_SAVE): the BK4819 asleep
 * (RADIO_RxSleep: REG_30 = 0, REG_37 = 0x1D00) for an off time, then awake to listen, on A and B in turn when dual
 * watch is on. A carrier keeps it listening up to DUTY_HOLD_MS so a tone can be checked; the squelch opening makes
 * sleep_tick busy, which ends the duty cycle and the radio receives as usual.
 *  - Battery save (MENU -> BAT SAVE): after BSAVE_IDLE_MS idle on the main screen, listen 100 ms, off n x 100 ms.
 *  - Sleep, after F4HWN's SetOff (MENU -> SLEEP, or SLEEP NOW): after the set time the last 10 s blink the
 *    backlight; then the panel sleeps, the backlight is off, listen SLEEP_LISTEN_MS, off SLEEP_OFF_MS; the green
 *    LED blinks every 5 s (F4HWN blinks its red one every 2 s). A key wakes it (and is not acted on), as does PTT.
 * F4HWN keeps the MCU clock as it is; so do we: the BK4819 and the backlight are the big loads. */
#define SLEEP_OFF_MS    3000u
#define SLEEP_LISTEN_MS  200u
#define SLEEP_WARN_MS  10000u
#define BSAVE_IDLE_MS  10000u
#define BSAVE_LISTEN_MS  100u
#define DUTY_HOLD_MS    1000u

static bool     s_asleep;
static uint32_t s_idle_sleep_ms;        /* how long nothing has happened (sleep_tick) */
static uint16_t s_blink_ms;
static uint16_t s_phase_ms;             /* time in this off or listen phase */
static bool     s_listening;            /* the receiver is awake for a listen window */
static bool     s_listen_other;         /* this window is on the other side (dual watch) */

/* Back to the active side: end a listen window on the other side first. */
static void duty_rx_active(void)
{
	if (s_peek)
		dw_end();                        /* the active side again (RADIO_Apply) */
}

static void duty_start(void)
{
	if (s_duty_on)
		return;
	s_duty_on = true;
	s_phase_ms = 0;
	s_listening = false;
	s_listen_other = false;
	RADIO_Monitor(false);
	duty_rx_active();
	RADIO_RxSleep();
}

static void duty_stop(void)
{
	if (!s_duty_on)
		return;
	s_duty_on = false;
	s_listening = false;
	/* a signal heard on the other side: back to the active side, and dual watch finds it again within its cycle */
	duty_rx_active();
	DW_Reset();
	if (RADIO_RxAsleep())
		RADIO_Apply();
}

static void duty_tick(uint16_t off_ms, uint16_t listen_ms)
{
	s_phase_ms = (uint16_t)(s_phase_ms + APP_TICK_MS);
	if (!s_listening) {
		if (s_phase_ms >= off_ms) {          /* listen: the next side in turn with dual watch on, else this one */
			s_phase_ms = 0;
			s_listening = true;
			s_listen_other = gStock.raw[SET_DUAL_WATCH] && !s_listen_other;
			if (s_listen_other)
				dw_begin();              /* the other side's settings, receiver awake (RADIO_Apply) */
			else
				RADIO_Apply();
		}
	} else if (s_phase_ms >= (RADIO_Carrier() ? DUTY_HOLD_MS : listen_ms)) {
		s_phase_ms = 0;                      /* nothing for this radio: off again */
		s_listening = false;
		duty_rx_active();
		RADIO_RxSleep();
	}
}

static void sleep_enter(void)
{
	s_asleep = true;
	s_blink_ms = 0;
	ST7735S_SetBacklight(false);
	s_light_on = false;
	ST7735S_Sleep(true);
#if APP_FM_ENABLE
	FMRX_BeepOff();                      /* no DAC current while asleep (an open FM radio keeps it) */
#endif
	if (s_screen != SCREEN_APP)          /* an app has the receiver (or the FM radio) to itself */
		duty_start();
}

static void sleep_wake(void)
{
	s_idle_sleep_ms = 0;
	duty_stop();
	if (!s_asleep)
		return;
	s_asleep = false;
	if (RADIO_State() != RADIO_RX_OPEN)
		IO_LedRx(false);
	ST7735S_Sleep(false);
#if APP_FM_ENABLE
	FMRX_BeepPrepare();                  /* the beeps' DAC again */
#endif
	s_redraw = true;                     /* the panel kept its picture, but draw it all again anyway */
	s_light_on = false;
	light_wake();
}

/* busy: something that keeps the radio awake (a key down, PTT, transmit, an open squelch, scanning, typing) */
static void sleep_tick(bool busy)
{
	uint32_t limit = (uint32_t)SLEEP_S[s_sleep_set < SLEEP_N ? s_sleep_set : SLEEP_DEFAULT] * 1000u;
	uint8_t bs = s_bsave < 4 ? BSAVE_OFF[s_bsave] : 0;

	if (busy) {
		sleep_wake();                    /* SLEEP NOW stays pending until the keys are let go */
		return;
	}
	if (s_sleep_now) {
		s_sleep_now = false;
		sleep_enter();
		return;
	}
	if (s_asleep) {                      /* the green LED on for 50 ms every 5 s */
		s_blink_ms = (uint16_t)(((unsigned)s_blink_ms + APP_TICK_MS) % 5000u);
		if (RADIO_State() != RADIO_RX_OPEN)
			IO_LedRx(s_blink_ms < 50u);
		if (s_screen != SCREEN_APP)
			duty_tick(SLEEP_OFF_MS, SLEEP_LISTEN_MS);
		return;
	}
	if (s_idle_sleep_ms < 0xFFFF0000u)
		s_idle_sleep_ms += APP_TICK_MS;
	if (limit) {
		if (s_idle_sleep_ms + SLEEP_WARN_MS >= limit && s_idle_sleep_ms < limit) {
			/* the last 10 s: the backlight on for 0.5 s every 2 s (as F4HWN) */
			ST7735S_SetBacklight((limit - s_idle_sleep_ms) % 2000u >= 1500u);
			s_light_on = false;          /* a key then lights it again (light_wake) */
		}
		if (s_idle_sleep_ms >= limit) {
			sleep_enter();
			return;
		}
	}
	if (bs && s_screen == SCREEN_MAIN && s_idle_sleep_ms >= BSAVE_IDLE_MS) {
		duty_start();
		duty_tick((uint16_t)(bs * 100u), BSAVE_LISTEN_MS);
	} else
		duty_stop();
}

/* A PTT pin counts as changed only after PTT_STABLE_MS of the same reading (it bounced on a radio). */
#define PTT_STABLE_MS 40u
#define PTT_ARM_MS    1000u        /* both PTTs released this long after power-on before either works */
static bool     s_ptt_armed;
static bool     s_tx_swapped;      /* the side a PTT transmits on was swapped in; swapped back when TX ends */
static uint16_t s_ptt_arm_ms;
static bool ptt_debounced(bool raw, unsigned i)
{
	static bool    state[2];
	static uint8_t ms[2];

	if (raw == state[i])
		ms[i] = 0;
	else if ((ms[i] += APP_TICK_MS) >= PTT_STABLE_MS) {
		state[i] = raw;
		ms[i] = 0;
	}
	return state[i];
}

/* what an app's key or tick returned */
static void app_result(AppsResult_t r)
{
	if (r == APPS_RELOAD) {              /* BT CHIRP wrote new channels and settings: as after a cable upload */
		start_from_stock();
		return;
	}
	if (r != APPS_STAY)
		app_close(r);
}

void APP_Tick(void)
{
	KEY_Code_t k;
	bool ptt;

	SYSTEM_WatchdogClear();
#if BT_LE_ENABLE
	BTLE_Tick();                         /* the Bluetooth controller, while an app has it on */
#endif

	if (PROGRAM_ENABLE && s_xf_ok && RADIO_State() != RADIO_TX) {
#if DEBUGGING
		uint8_t mb[32];
		int mn = UART_Receive(mb, sizeof mb, 0);
		int p;
		MON_Feed(mb, mn);                /* the PC's monitor commands ('!' lines), then CHIRP sees the same bytes */
		p = PROGRAM_PollBytes(mb, mn);
#else
		int p = PROGRAM_Poll();          /* a whole CHIRP session runs inside this call */
#endif
		if (p == PROGRAM_DONE_WRITTEN)
			start_from_stock();          /* new channels and settings */
		if (p != PROGRAM_IDLE) {
			go(SCREEN_MAIN);
			return;
		}
	}

	/* power switch: PB11 high = off (stock sub_01E4C978); act after 0.2 s */
	if (GPIO_CheckBit(PIN_POWER_SENSE)) {
		if ((s_off_ticks = (uint16_t)(s_off_ticks + APP_TICK_MS)) >= 200u) {
			RADIO_EndTx();
			if (s_app) {                 /* its leave puts back what it changed (the APRS app's VFO) */
				const AppDef_t *a = s_app;
				s_app = 0;
				a->leave();
			}
#if SETTINGS_ENABLE_SAVE
			if (s_save_due || s_fm_wait) {   /* a change not saved yet (key still held): save it now */
				dw_end();
				save_now();
			}
#endif
			IO_Amp(0xFF, false);
			BOARD_PowerOff();
		}
	} else
		s_off_ticks = 0;

	/* PTT; PTT-A transmits on side A and PTT-B on side B (see below). Neither counts until both have read released for
	 * PTT_ARM_MS since power-on: on a radio (2026-11-24) the radio switched to B and keyed 1-3 short blips at every
	 * start-up, so PTT-B (PA12) reads low for a while as the board powers up. A PTT held at power-on is ignored
	 * until it is let go. */
	bool raw_a = KEYBOARD_IsPttPressed(), raw_b = KEYBOARD_IsPtt2Pressed();
	bool ptt_a = ptt_debounced(raw_a, 0), ptt_b = ptt_debounced(raw_b, 1);
	if (!s_ptt_armed) {
		if (raw_a || raw_b)
			s_ptt_arm_ms = 0;
		else if ((s_ptt_arm_ms += APP_TICK_MS) >= PTT_ARM_MS)
			s_ptt_armed = true;
		ptt_a = ptt_b = false;
	}
	/* LOCK PTT on and the keys locked: PTT does nothing (said once a press) */
	if (s_lock_ptt && gStock.raw[SET_KEY_LOCK] && !s_ptt_down) {
		static bool said;
		if ((ptt_a || ptt_b) && !said)
			note("LOCKED: HOLD #", COLOR_WARN);
		said = ptt_a || ptt_b;
		ptt_a = ptt_b = false;
	}
	ptt = ptt_a || ptt_b;
	if (ptt && s_screen == SCREEN_APP && s_app && s_app->ptt_stays)
		s_ptt_here = true;               /* the signal meter: transmit on its side, the screen stays */
	else if (ptt && s_screen == SCREEN_APP) { /* PTT leaves an app (the spectrum has the receiver) */
		const AppDef_t *a = s_app;
		s_app = 0;
		if (a)
			a->leave();
		if (a && a->ptt_tunes) {
			/* the spectrum: transmit where it is tuned, on the selected side, whichever PTT (Sean, 2026-11-24) */
			if (gAppsTune && gAppsTune != gVfo.freq) {
				s_mr = false;
				s_ch = -1;
				gVfo.tx_freq = 0;
				gVfo.rx_only = false;
				vfo_retune(gAppsTune);
				RADIO_Apply();
			}
			s_ptt_here = true;
		}
		go(SCREEN_MAIN);
	}
	if (ptt) {
		dw_end();
		tscan_stop();
	}
	if (ptt && !s_ptt_down && (s_tail_ms || s_eot)) {   /* pressed again during the tail: start over */
		s_tail_ms = 0;
		s_eot = false;
		DTMFSEQ_Stop();
		RADIO_EndTx();
	}
	if (ptt && !s_ptt_down) {
		/* each PTT transmits on its own side (A wins if both), without changing the selection: the other side is
		 * swapped in for the transmission and swapped back when it ends (s_tx_swapped). Only A/B changes the
		 * selected side (Sean, 2026-11-24: PTT-B used to make B the selected side, and PTT-A sent on B then). */
		uint8_t want = s_ptt_here ? s_active : ptt_a ? 0u : 1u;
		if (s_active != want) {
			side_switch();
			s_tx_swapped = !s_tx_swapped;
		}
		light_wake();
		s_tot_hit = false;
		s_tx_why = tx_busy_locked() ? "NO TX: BUSY" : tx_batt_locked() ? "NO TX: BATTERY LOW" : RADIO_TxRefusal();
		s_tx_refused = s_tx_why || !RADIO_StartTx();
		if (s_tx_refused)
			RADIO_Beep(400, 150);
		else {
			ptt_id_start(1);
			if (DEBUG_ON)                    /* what keyed it (Sean, 2026-11-27: TX on EXIT and on blowing into the mic) */
				note(ptt_a && ptt_b ? "TX BY PTT-A+B" : ptt_a ? "TX BY PTT-A" : "TX BY PTT-B", COLOR_WARN);
		}
	} else if (!ptt && s_ptt_down) {
		if (RADIO_State() == RADIO_TX) {
			DTMFSEQ_Stop();                /* an up code still going is cut short */
			RADIO_TxToneOff();
			s_eot = ptt_id_start(2);
			if (!s_eot)
				tx_release();
		}
		s_tx_refused = s_tot_hit = false;
	}
	s_ptt_down = ptt;
	if (!ptt)
		s_ptt_here = false;

	if (RADIO_State() != RADIO_TX) {
		DTMFSEQ_Stop();
		s_eot = false;
	} else
		ptt_id_tick();
	if (s_tail_ms && (s_tail_ms = s_tail_ms > APP_TICK_MS ? (uint16_t)(s_tail_ms - APP_TICK_MS) : 0) == 0) {
		RADIO_EndTx();
		/* stock 0x01E4FF94: setting 0x15 x 100 ms with the receive audio held off */
		RADIO_MuteRx((uint16_t)((gStock.raw[SET_RPT_CLEAR] < 30 ? gStock.raw[SET_RPT_CLEAR] : 30) * 100u));
	}
	if (s_tx_swapped && !ptt && !s_tail_ms && !s_eot && RADIO_State() != RADIO_TX) {
		side_switch();                     /* the transmission on the other side is over: back to the selected one */
		s_tx_swapped = false;
	}
	{
		/* radio.c counts in 10 ms steps (beep length, squelch open / close, tone checks): one step each 10 ms of real
		 * time, however often this tick runs (Sean, 2026-11-27: at 2 ms ticks the beeps ran 5x long) */
		static uint16_t radio_ms;
		radio_ms = (uint16_t)(radio_ms + APP_TICK_MS);
		while (radio_ms >= 10u) {
			radio_ms = (uint16_t)(radio_ms - 10u);
			RADIO_Tick();
		}
	}
	gAppsUptimeMs += APP_TICK_MS;
#if APP_FM_ENABLE
	FMRX_BeepTick(APP_TICK_MS);          /* keeps the beep's samples coming; the DAC off after 3 s quiet */
#endif
	{
		static RadioState_t last;
		RadioState_t st = RADIO_State();
		if (st != last)
			s_trx_ms = 0;
		else if (st != RADIO_RX && s_trx_ms < 0xFFFF0000u)
			s_trx_ms += APP_TICK_MS;
		last = st;
	}
	tot_tick();
	tscan_tick();
	dtmf_tick();
#if RADIO_ENABLE_TX
	/* VOX (stock setting 0x3A), not while PTT is held, scanning or in a menu, nor before PTT is armed at start-up */
	if (gStock.raw[0x3A] == 1u && s_ptt_armed && !ptt && !SCAN_Active() && !s_tscan && s_screen == SCREEN_MAIN) {
		uint16_t a = ADC_Read(ADC_CH_PA1, 1);
		VoxAction_t v = VOX_Tick(a == 0xFFFFu ? 0 : (uint8_t)(a >> 2), gStock.raw[0x02], gStock.raw[0x20],
		                         RADIO_State() == RADIO_TX, APP_TICK_MS);
		if (v == VOX_START && !tx_busy_locked() && !tx_batt_locked()) {
			dw_end();
			RADIO_StartTx();
			if (DEBUG_ON)
				note("TX BY VOX", COLOR_WARN);
		} else if (v == VOX_STOP)
			RADIO_EndTx();
	}
#endif
	if (!s_duty_on)
		dw_tick(ptt || RADIO_MonitorOn() || s_tscan || SCAN_Active() || s_entry_len || s_screen != SCREEN_MAIN);
	if (RADIO_State() == RADIO_RX_OPEN)
		light_wake();
	else
		light_tick();
	if (ptt)
		SCAN_Stop();
	else if ((SCAN_SetTick(APP_TICK_MS), SCAN_Tick(RADIO_State() == RADIO_RX_OPEN)) == SCAN_HOP)
		scan_hop();

	/* battery about once a second, never while transmitting (the voltage sags) */
	if ((s_batt_ticks = (uint16_t)(s_batt_ticks + APP_TICK_MS)) >= 1000u && RADIO_State() != RADIO_TX) {
		s_batt_ticks = 0;
		BATTERY_Set(ADC_Read(ADC_CH_BATTERY, 8));
	}
	{
		/* under 5 %: "BATTERY LOW" and a low beep at once, then every minute (not while transmitting or asleep) */
		static uint32_t warn_ms = 60000u;
		if (BATTERY_Raw() && BATTERY_Percent() < 5) {
			if ((warn_ms += APP_TICK_MS) >= 60000u && RADIO_State() != RADIO_TX && !s_asleep) {
				warn_ms = 0;
				note("BATTERY LOW", COLOR_TX);
				RADIO_Beep(500, 300);
			}
		} else
			warn_ms = 60000u;
	}

	k = key_event();
	if (s_asleep && (k != KEY_INVALID || s_key_held != KEY_INVALID)) {
		sleep_wake();                      /* the key that wakes the radio does nothing else */
		k = KEY_INVALID;
		s_wait_release = true;
	}
	/* while transmitting, a held digit, * or # sends its DTMF pair (stock 0x01E5457E / 0x01E5072C) */
	if (RADIO_State() == RADIO_TX && DTMFSEQ_Busy())
		k = KEY_INVALID;                   /* keys wait while a PTT-ID is sent */
	else if (RADIO_State() == RADIO_TX) {
		int d = KEY_Digit(s_key_held);

		if (s_key_held == KEY_STAR)
			d = 14;
		else if (s_key_held == KEY_HASH)
			d = 15;
		else if (s_key_held == KEY_EQUALS)
			d = 19;                        /* = held: the 1750 Hz repeater tone burst (K5 feature 1.14) */
		if (d >= 0) {
			if (k != KEY_INVALID)
				RADIO_TxTone((uint8_t)d, gStock.raw[SET_DTMF_MON] & 1u);
			k = KEY_INVALID;
		} else if (RADIO_TxToneOn())
			RADIO_TxToneOff();
	}
	if (k != KEY_INVALID || s_key_held != KEY_INVALID)   /* a press lights the screen and still does its job */
		light_wake();
	/* Key lock (stock setting 0x1B, kept across power-off): # held 1 s locks and unlocks; while locked, only PTT
	 * works. Auto lock (setting 0x10): locks after KEY_AUTOLOCK_MS on the main screen with no key (our timing). */
	if (k == KEY_ACT_LOCK) {
		gStock.raw[SET_KEY_LOCK] = !gStock.raw[SET_KEY_LOCK];
		note(gStock.raw[SET_KEY_LOCK] ? "KEYS LOCKED" : "KEYS UNLOCKED", COLOR_WARN);
		RADIO_Beep(gStock.raw[SET_KEY_LOCK] ? 800 : 1200, 60);
#if SETTINGS_ENABLE_SAVE
		save_soon();
#endif
		s_idle_ms = 0;
		k = KEY_INVALID;
	} else if (k != KEY_INVALID && gStock.raw[SET_KEY_LOCK] && k != KEY_ACT_LAMP && k != KEY_LONG(KEY_ACT_LAMP)
	           && !(s_screen == SCREEN_HELP && k == KEY_ACT_EXIT)) {
		/* the flashlight and its help still work while locked */
		if (k != KEY_HASH)
			note("LOCKED: HOLD #", COLOR_WARN);
		k = KEY_INVALID;
	}
	if (k != KEY_INVALID || ptt || s_screen != SCREEN_MAIN || s_entry_len)
		s_idle_ms = 0;
	else if (gStock.raw[SET_AUTO_LOCK] && !gStock.raw[SET_KEY_LOCK] && (s_idle_ms += APP_TICK_MS) >= KEY_AUTOLOCK_MS) {
		gStock.raw[SET_KEY_LOCK] = 1;
		note("KEYS LOCKED", COLOR_WARN);
#if SETTINGS_ENABLE_SAVE
		save_soon();
#endif
	}
	/* menu time-out (stock setting 0x21: 5-50 s in 5 s steps, then 60 s), not while a value is being edited */
	if (k != KEY_INVALID || s_screen == SCREEN_MAIN || s_screen == SCREEN_APP || s_screen == SCREEN_HELP
	    || s_menu_edit || s_menu_hidden)
		s_menu_ms = 0;
	else if ((s_menu_ms += APP_TICK_MS) >= (gStock.raw[0x21] < 10 ? 5000u * (gStock.raw[0x21] + 1u) : 60000u)) {
		s_menu_ms = 0;
		go(SCREEN_MAIN);
	}
	if (k != KEY_INVALID)
		dw_end();                          /* keys act on the active side */
#if SETTINGS_ENABLE_SAVE
	if (k != KEY_INVALID)
		save_soon();
	if (s_fm_wait && (s_fm_wait = s_fm_wait > APP_TICK_MS ? (uint16_t)(s_fm_wait - APP_TICK_MS) : 0) == 0)
		save_soon();                     /* the FM station has settled */
	save_tick(ptt || s_tx_swapped || s_peek || s_tscan || SCAN_Active() || s_entry_len);
#endif
	if (s_note_ms)
		s_note_ms = s_note_ms > APP_TICK_MS ? s_note_ms - APP_TICK_MS : 0;
	if (s_lamp_mode == 2 && (s_lamp_ms += APP_TICK_MS) >= LAMP_STROBE_MS) {
		s_lamp_ms = 0;
		IO_Lamp(!IO_LampIsOn());
	}
	if (k != KEY_INVALID && (k != KEY_EQUALS || s_screen == SCREEN_APP) && !ptt && !s_tx_swapped) {
		/* = is eq_tick's on the main screen; an app gets it as a key */
		if (gStock.raw[SET_BEEP])
			RADIO_Beep(1000, 40);
		if (k == KEY_ACT_LAMP)                   /* every screen: tap the light, hold for the keys */
			lamp_step();
		else if (k == KEY_LONG(KEY_ACT_LAMP))
			help_open();
		else if (s_screen == SCREEN_HELP) {
			if (k == KEY_ACT_EXIT)
				help_close();
		} else if (k == KEY_HASH && !(s_screen == SCREEN_MENU && s_menu_edit && s_menu_sel == M_CHNAME))
			s_fkey = !s_fkey;                    /* F; # still moves the cursor in a channel name */
		else if (s_screen == SCREEN_MAIN)
			key_main(k);
		else if (s_screen == SCREEN_APP) {
			KEY_Code_t a = KEY_BASE(k);
			AppsResult_t r;
			if (s_fkey && a == KEY_ACT_UP)       /* F in an app: UP / DOWN are the app's # and * */
				a = KEY_HASH;
			else if (s_fkey && a == KEY_ACT_DOWN)
				a = KEY_STAR;
			s_fkey = false;                      /* F is for one key, then off */
			r = s_app ? s_app->key(a) : APPS_EXIT;
			app_result(r);
		} else if (s_screen == SCREEN_ABOUT) {
			s_fkey = false;
#if DEBUGGING
			if (k == KEY_STAR) {             /* rescue mode: the serial (USB) lines let go, then the restart */
				UI_PrintPadded("RESCUE...", 58, 105, COLOR_WARN, COLOR_BG, 11);
				reboot_to_rescue();
			}
#endif
			if (DEBUGGING && k == KEY_ACT_MENU) {   /* the debug switch, kept with the settings */
				gDebug = !gDebug;
				s_redraw = true;
#if SETTINGS_ENABLE_SAVE
				save_soon();
#endif
			} else
				go(SCREEN_MENU);
		}
		else {
			s_fkey = false;
			key_menu(KEY_BASE(k));
		}
	}

	eq_tick(ptt);

	{
		bool saving = false;
#if SETTINGS_ENABLE_SAVE
		saving = s_save_due;             /* a change still to be written: stay awake until it is */
#endif
		/* every screen sleeps the same way (Sean, 2026-11-27); an app keeps running with its screen off */
		sleep_tick(ptt || s_key_held != KEY_INVALID || RADIO_State() != RADIO_RX || SCAN_Active() || s_tscan
		           || s_entry_len || s_eot || s_tail_ms || saving);
	}
	status_fill();
	if (s_redraw)
		UI_StatusHide();                   /* the screen draws its bar again, or has none */
	if (s_screen == SCREEN_APP) {          /* asleep too: the FM radio plays on */
		AppsResult_t r = s_app ? s_app->tick(s_redraw && !s_asleep) : APPS_EXIT;
		app_result(r);
	} else if (s_asleep)
		;                                  /* the panel is off: nothing drawn */
	else if (s_screen == SCREEN_MAIN) {
		draw_main();
	} else if (s_redraw && s_screen == SCREEN_ABOUT)
		draw_about();
	else if (s_redraw && s_screen == SCREEN_HELP)
		draw_help();
	else if (s_redraw && s_screen != SCREEN_HELP)
		draw_menu();
	if (!s_asleep && s_screen != SCREEN_MAIN)
		UI_StatusRefresh();
	s_redraw = false;
}

#if DEBUGGING
/* !state on the cable (app/monitor.h) */
void APP_MonState(char *out, unsigned max)
{
	static const char *const SCR[] = { "MAIN", "MENU", "ABOUT", "APP", "HELP" };
	char b[160], *o = b;
	const char *q;
	uint16_t r = RADIO_Rssi();

	for (q = "screen "; *q; ) *o++ = *q++;
	for (q = SCR[s_screen]; *q; ) *o++ = *q++;
	if (s_screen == SCREEN_APP && s_app) {
		for (q = " app "; *q; ) *o++ = *q++;
		for (q = s_app->name; *q; q++)
			*o++ = *q == ' ' ? '_' : *q;
	}
	for (q = " freq "; *q; ) *o++ = *q++;
	o = APPS_FormatMhz(o, gVfo.freq);
	for (q = RADIO_State() == RADIO_TX ? " TX" : " RX"; *q; ) *o++ = *q++;
	for (q = " rssi "; *q; ) *o++ = *q++;
	o = APPS_PutInt(o, r ? (int)(r / 2u) - 160 : -160);
	for (q = RADIO_Carrier() ? " carrier" : " quiet"; *q; ) *o++ = *q++;
	for (q = s_mr ? " MR " : " VFO "; *q; ) *o++ = *q++;
	o = APPS_PutInt(o, s_ch);
	for (q = " up_ms "; *q; ) *o++ = *q++;
	o = APPS_PutInt(o, (int)gAppsUptimeMs);
	for (q = " tick "; *q; ) *o++ = *q++;
	o = APPS_PutInt(o, gAppTickMs);
	*o = 0;
	{
		unsigned i = 0;
		for (; i + 1u < max && b[i]; i++)
			out[i] = b[i];
		out[i] = 0;
	}
}
#endif
