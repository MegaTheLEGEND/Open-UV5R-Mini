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
#include "store/calib.h"
#include "store/channels.h"
#include "app/keys.h"
#include "app/keytest.h"
#include "app/program.h"
#include "app/version.h"
#include "radio/radio.h"
#include "radio/dualwatch.h"
#include "radio/dtmfseq.h"
#include "radio/scan.h"
#include "radio/vox.h"
#include "store/settings.h"
#include "radio/tones.h"
#include "update/update.h"
#include "board.h"
#include "bt/bt.h"
#include "driver/adc.h"
#include "driver/bk4819.h"
#include "driver/extflash.h"
#include "driver/gpio.h"
#include "driver/io.h"
#include "driver/keyboard.h"
#include "driver/st7735s.h"
#include "driver/system.h"
#include "ui/helper.h"
#include "ui/screen_main.h"
#include "ui/theme.h"


typedef enum { SCREEN_MAIN, SCREEN_MENU, SCREEN_ABOUT } Screen_t;

static Screen_t s_screen;
static bool     s_redraw = true;         /* whole screen */
#define LAMP_STROBE_MS 500u          /* strobe half-period: a guess, the stock rate is not read yet */
static uint8_t  s_lamp_mode;             /* 0 off, 1 on, 2 strobe (stock flashlight key) */
static uint16_t s_lamp_ms;
static char     s_entry[7];              /* digits typed, MHz then kHz */
static uint8_t  s_entry_len;
static uint8_t  s_menu_sel, s_menu_edit;
static int      s_menu_top = -1;
static uint16_t s_off_ticks, s_batt_ticks;
static uint32_t s_light_ms, s_tx_ms;     /* backlight on for, transmitting for */
static uint32_t s_idle_ms;                /* main screen with no key, for auto lock */
static bool     s_light_on = true, s_tot_hit;
static bool     s_mr;                    /* channel mode */
static int      s_ch = -1;               /* channel slot shown, 0-based */
static char     s_ch_name[13];
static bool     s_xf_ok;                 /* external flash answered */
static bool     s_ptt_down, s_tx_refused;
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
static uint16_t s_dw_peeks, s_dw_holds;  /* dual watch test counts (shown on the bottom line) */
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

static int8_t meter_bars(void)          /* RSSI to 0..7 bars: about -125 dBm and up, 9 dB a bar */
{
	int dbm = (int)RADIO_Rssi() / 2 - 160, n = (dbm + 125) / 9;

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
		else if (dm == 0 && name[0])
			copy_text(u->big, name, sizeof u->big);
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

static void main_view(UiMain_t *v)
{
	uint8_t other = (uint8_t)(s_active ^ 1u), tuned = (uint8_t)(s_active ^ (s_peek ? 1u : 0u));
	const Side_t *o = &s_side[other];
	RadioState_t st = RADIO_State();
	int lvl = BATTERY_Level();
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

	v->volume = gVfo.volume;
	v->squelch = gVfo.squelch;
	v->dual_watch = gStock.raw[SET_DUAL_WATCH] != 0;
	v->scan = SCAN_Active() || SCAN_Holding();
	v->scan_hold = SCAN_Holding();
	v->lamp = s_lamp_mode != 0;
	v->strobe = s_lamp_mode == 2;
	v->warning = !CALIB_Present();
	v->locked = gStock.raw[SET_KEY_LOCK] != 0;
	v->battery = (int8_t)(lvl == 0xFF || !BATTERY_Raw() ? -1 : (lvl * 4 + 5) / 6);
	v->battery_low = BATTERY_Low();
	v->battery_hide = v->battery_low && (s_batt_ticks * APP_TICK_MS) % 1000u < 500u;

	/* bottom line, most important first */
	v->bottom_color = COLOR_DIM;
	if (s_tx_refused) {
		copy_text(v->bottom, s_tx_why ? s_tx_why : "NO TX HERE", sizeof v->bottom);
		v->bottom_color = COLOR_TX;
	} else if (s_note_ms && s_note) {
		copy_text(v->bottom, s_note, sizeof v->bottom);
		v->bottom_color = s_note_color;
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
	} else if (s_dtmf_len) {
		char *p = v->bottom;
		*p++ = 'D'; *p++ = 'T'; *p++ = 'M'; *p++ = 'F'; *p++ = ' ';
		copy_text(p, s_dtmf, 17);
		v->bottom_color = COLOR_WARN;
	} else if (s_entry_len)
		copy_text(v->bottom, "# ENTER  EXIT CLEAR", sizeof v->bottom);
	else if (gStock.raw[SET_DUAL_WATCH]) {
		/* dual watch test line: how many times it looked at the other side, and how many times it stayed there
		 * for a signal (radio, 2026-11-22: dual watch never received; this says whether it switches at all) */
		char *p = v->bottom;
		*p++ = 'D'; *p++ = 'W'; *p++ = ' '; *p++ = 'P';
		p = put_num(p, s_dw_peeks % 10000u);
		*p++ = ' '; *p++ = 'H';
		p = put_num(p, s_dw_holds % 10000u);
		*p = 0;
	}
	else if (s_mr)
		copy_text(v->bottom, "MENU *BW  HOLD# LOCK", sizeof v->bottom);
	else {
		char *p = v->bottom;
		const char *q = "STEP ";
		while (*q)
			*p++ = *q++;
		copy_text(p, fmt_step(b, RADIO_Steps[gVfo.step]), 8);
	}
}

static void draw_main(void)
{
	main_view(&s_view);
	UI_MainDraw(&s_view, s_redraw);
}

/* ------------------------------------------------------------------------------------------- menu */
enum { M_AB, M_DW, M_MEM, M_SCAN, M_SCANMODE, M_SQL, M_VOL, M_STEP, M_BW, M_MOD, M_RXTONE, M_TXTONE, M_SHIFT,
       M_OFFSET, M_POWER, M_TSCAN, M_TSCANSAVE, M_CHSAVE, M_CHDEL, M_SCANADD, M_SET0 };
static const char *const MENU_NAMES[M_SET0] = {
	"A/B", "DUAL WATCH", "MEMORY", "SCAN", "SCAN MODE", "SQUELCH", "VOLUME", "STEP", "BANDWIDTH", "MODULATION",
	"RX TONE", "TX TONE", "SHIFT", "OFFSET", "TX POWER", "TONE SCAN", "TSCAN SAVE", "SAVE CH", "DEL CH", "SCAN ADD",
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
};
#define SET_COUNT  ((int)(sizeof SET_ITEMS / sizeof SET_ITEMS[0]))
#define M_KEYTEST  (M_SET0 + SET_COUNT)
#define M_ABOUT    (M_KEYTEST + 1)
#define M_COUNT    (M_ABOUT + 1)
#define MENU_ROWS 8                      /* rows on screen; the list scrolls */

static const char *menu_name(int i)
{
	if (i < M_SET0)
		return MENU_NAMES[i];
	if (i < M_KEYTEST)
		return SET_ITEMS[i - M_SET0].name;
	return i == M_KEYTEST ? "KEY TEST" : "ABOUT";
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

static const char *menu_value(int i, char *b)
{
	static const char *const MOD[] = { "AUTO", "FM", "AM" };

	if (i >= M_SET0 && i < M_KEYTEST)
		return set_value(&SET_ITEMS[i - M_SET0], b);
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
	case M_ABOUT:   return FW_VERSION;
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
	if (i >= M_SET0 && i < M_KEYTEST) {
		const SetItem_t *it = &SET_ITEMS[i - M_SET0];
		uint8_t v = gStock.raw[it->off] < it->count ? gStock.raw[it->off] : 0;
		gStock.raw[it->off] = (uint8_t)((v + it->count + dir) % it->count);
		if (it->off == 0x3A)
			VOX_Reset();
		return;
	}
	switch (i) {
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
	char b[12];

	if (s_redraw) {
		ST7735S_FillRect(0, 0, 128, 128, COLOR_BG);
		UI_PrintString("MENU", 52, 2, COLOR_WARN, COLOR_BG, 1);
		UI_PrintString("UP/DN MENU EXIT", 0, 120, COLOR_DIM, COLOR_BG, 1);
	}
	int top = s_menu_sel < MENU_ROWS ? 0 : s_menu_sel - MENU_ROWS + 1;

	if (top != s_menu_top) {
		s_menu_top = top;
		ST7735S_FillRect(0, 14, 128, 12 * MENU_ROWS + 2, COLOR_BG);
	}
	for (int i = top; i < M_COUNT && i < top + MENU_ROWS; i++) {
		uint8_t y = (uint8_t)(16 + 12 * (i - top));
		bool sel = i == s_menu_sel;
		uint16_t bg = sel ? COLOR_BAR : COLOR_BG;
		ST7735S_FillRect(0, y - 1, 128, 10, bg);
		UI_PrintString(menu_name(i), 2, y, COLOR_TEXT, bg, 1);
		UI_PrintString(menu_value(i, b), 76, y, sel && s_menu_edit ? COLOR_RX : COLOR_WARN, bg, 1);
	}
}

/* ------------------------------------------------------------------------------------------- keys */
static void go(Screen_t s)
{
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
		s_redraw = true;
		channel_show(n >= 1 && n <= CHANNEL_COUNT ? (int)n - 1 : -1);
		return;
	}
	uint32_t khz = 0;

	for (int i = 0; i < 6; i++)
		khz = khz * 10 + (uint32_t)(i < s_entry_len ? s_entry[i] - '0' : 0);
	s_entry_len = 0;
	if (khz * 100u < FREQ_MIN || khz * 100u > FREQ_MAX) {
		RADIO_Beep(400, 120);
		s_redraw = true;
		return;
	}
	vfo_retune(khz * 100u);
	RADIO_Apply();
	s_redraw = true;
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

static void scan_start(int dir)
{
	uint8_t m = gStock.raw[SET_SCAN_MODE];

	SCAN_Start(dir, (ScanResume_t)(m <= 2 ? m : 1), APP_TICK_MS);
	s_entry_len = 0;
}

static void key_main(KEY_Code_t k)
{
	int d = KEY_Digit(k);

	if (s_tscan) {                       /* any key ends the tone scan */
		tscan_stop();
		return;
	}
	if (SCAN_Active()) {                 /* any key ends the scan; UP/DOWN turn it round */
		if (k == KEY_ACT_UP || k == KEY_ACT_DOWN)
			scan_start(k == KEY_ACT_UP ? +1 : -1);
		else
			SCAN_Stop();
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
			s_redraw = true;
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
		go(SCREEN_MENU);
	} else if (k == KEY_ACT_BW) {
		gVfo.narrow = !gVfo.narrow;
		RADIO_Apply();
	} else if (k == KEY_ACT_LAMP) {
		/* stock 0x01E537C8: the flashlight key steps off, on, strobe (RAM 0x198C0+2831, mod 3) */
		s_lamp_mode = (uint8_t)((s_lamp_mode + 1u) % 3u);
		s_lamp_ms = 0;
		IO_Lamp(s_lamp_mode != 0);
	}
	else if (k == KEY_ACT_AB || k == KEY_ACT_VM) {
		menu_change(k == KEY_ACT_AB ? M_AB : M_MEM, 0);
		s_redraw = true;
	}
}

/* MENU -> ABOUT: name, version and build; any key goes back to the menu. */
static void draw_about(void)
{
	ST7735S_FillRect(0, 0, 128, 128, COLOR_BG);
	ST7735S_FillRect(0, 0, 128, 11, COLOR_BAR);
	UI_PrintString("ABOUT", 49, 2, COLOR_TEXT, COLOR_BAR, 1);
	UI_PrintString(FW_NAME, 22, 20, COLOR_ACCENT, COLOR_BG, 1);
	UI_PrintString("VERSION", 4, 38, COLOR_DIM, COLOR_BG, 1);
	UI_PrintString(FW_VERSION, 58, 38, COLOR_TEXT, COLOR_BG, 1);
	UI_PrintString("BUILD", 4, 52, COLOR_DIM, COLOR_BG, 1);
	UI_PrintString(VERSION_Build(), 58, 52, COLOR_TEXT, COLOR_BG, 1);
	UI_PrintString("RADIO", 4, 66, COLOR_DIM, COLOR_BG, 1);
	UI_PrintString(FW_TARGET, 4, 78, COLOR_TEXT, COLOR_BG, 1);
	UI_PrintString("OPEN FIRMWARE", 4, 96, COLOR_DIM, COLOR_BG, 1);
	UI_PrintString("ANY KEY: BACK", 0, 118, COLOR_DIM, COLOR_BG, 1);
}

/* MENU on a row: rows that act at once, rows that ask first (SAVE CH, DEL CH), or edit with UP/DOWN. */
static void menu_enter(void)
{
	switch (s_menu_sel) {
	case M_KEYTEST:
		KEYTEST_Run();
		s_redraw = true;
		return;
	case M_ABOUT:
		go(SCREEN_ABOUT);
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

static void key_menu(KEY_Code_t k)
{
	int d = KEY_Digit(k);

	if (k == KEY_ACT_EXIT) {
		if (s_menu_edit)
			s_menu_edit = 0;
		else
			go(SCREEN_MAIN);
	} else if (k == KEY_ACT_MENU) {
		menu_enter();
		if (s_screen != SCREEN_MENU)
			return;
	} else if (k == KEY_ACT_UP || k == KEY_ACT_DOWN) {
		int dir = k == KEY_ACT_UP ? -1 : 1;
		if (s_menu_edit)
			menu_change(s_menu_sel, -dir);
		else
			s_menu_sel = (uint8_t)((s_menu_sel + M_COUNT + dir) % M_COUNT);
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

static KEY_Code_t key_event(void)
{
	static KEY_Code_t last = KEY_INVALID;
	static uint16_t held;
	KEY_Code_t k = KEYBOARD_Poll();

	s_key_held = k;
	if (k != last) {
		last = k;
		held = 0;
		return k;
	}
	if (k == KEY_INVALID)
		return KEY_INVALID;
	if (held < 0xFFFFu)
		held++;
	if (k == KEY_ACT_UP || k == KEY_ACT_DOWN) {
		if (held >= 50 && held % 10 == 0)
			return k;
	}
	if (k == KEY_HASH && held == KEY_LOCK_MS / APP_TICK_MS)
		return KEY_ACT_LOCK;                 /* # held: lock or unlock the keys */
	return KEY_INVALID;
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
	EXT_MAGIC_VALUE = 0xA5,
};

static bool ext_ok(void)
{
	return gStock.ext[EXT_MAGIC] == EXT_MAGIC_VALUE;
}

static void start_side(uint8_t side);

static void start_from_stock(void)
{
	SETTINGS_Defaults();
	s_active = 0;
	side_save();                            /* without stock settings both sides start as the default VFO */
	s_side[1] = s_side[0];
	if (!s_xf_ok || !SETTINGS_Load())
		return;
	if (gStock.raw[SET_SQUELCH] <= 9)
		gVfo.squelch = gStock.raw[SET_SQUELCH];
	if (ext_ok() && gStock.ext[EXT_VOLUME] <= 15)
		gVfo.volume = gStock.ext[EXT_VOLUME];
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
	s_redraw = true;
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
	s_dw_peeks++;
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
		s_dw_holds++;
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
	if (!s_save_due || busy || s_key_held != KEY_INVALID)
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
	ST7735S_Init();
	KEYBOARD_Init();
	if (UPDATE_Requested())
		UPDATE_Mode();          /* PTT-A + '=' at power-on, as the stock app; does not return */
	BK4819_Init();
	/* a top-row key (wired to GND) held at power-on, PTT not held: the key test */
	if (!KEYBOARD_IsPttPressed()) {
		KEY_Code_t k0 = KEYBOARD_Poll();
		if (k0 == KEY_DIRECT0 || k0 == KEY_VM || k0 == KEY_AB || k0 == KEY_LAMP)
			KEYTEST_Run();
	}
	EXTFLASH_Init();
	s_xf_ok = EXTFLASH_ReadId() == EXTFLASH_ID;
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
	BT_Init(0);
	go(SCREEN_MAIN);
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
	if (tot && tot <= 12 && (s_tx_ms += APP_TICK_MS) >= 15000u * tot) {
		RADIO_EndTx();
		s_tot_hit = s_tx_refused = true;
		s_tx_why = "TX TIME OUT";
		RADIO_Beep(400, 300);
	}
}

/* A PTT pin counts as changed only after PTT_STABLE_MS of the same reading (it bounced on a radio). */
#define PTT_STABLE_MS 40u
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

void APP_Tick(void)
{
	KEY_Code_t k;
	bool ptt;

	SYSTEM_WatchdogClear();

	if (PROGRAM_ENABLE && s_xf_ok && RADIO_State() != RADIO_TX) {
		int p = PROGRAM_Poll();          /* a whole CHIRP session runs inside this call */
		if (p == PROGRAM_DONE_WRITTEN)
			start_from_stock();          /* new channels and settings */
		if (p != PROGRAM_IDLE) {
			go(SCREEN_MAIN);
			return;
		}
	}

	/* power switch: PB11 high = off (stock sub_01E4C978); act after 0.2 s */
	if (GPIO_CheckBit(PIN_POWER_SENSE)) {
		if (++s_off_ticks >= 200 / APP_TICK_MS) {
			RADIO_EndTx();
#if SETTINGS_ENABLE_SAVE
			if (s_save_due) {            /* a change not saved yet (key still held): save it now */
				dw_end();
				save_now();
			}
#endif
			IO_Amp(0xFF, false);
			BOARD_PowerOff();
		}
	} else
		s_off_ticks = 0;

	/* PTT; PTT-B transmits on side B (it becomes the active side) */
	ptt = ptt_debounced(KEYBOARD_IsPttPressed(), 0);
	if (ptt_debounced(KEYBOARD_IsPtt2Pressed(), 1)) {
		if (!s_ptt_down && s_active != 1) {
			side_switch();
#if SETTINGS_ENABLE_SAVE
			save_soon();
#endif
		}
		ptt = true;
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
		light_wake();
		s_tot_hit = false;
		s_tx_why = tx_busy_locked() ? "NO TX: BUSY" : RADIO_TxRefusal();
		s_tx_refused = s_tx_why || !RADIO_StartTx();
		if (s_tx_refused)
			RADIO_Beep(400, 150);
		else
			ptt_id_start(1);
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
	RADIO_Tick();
	tot_tick();
	tscan_tick();
	dtmf_tick();
#if RADIO_ENABLE_TX
	/* VOX (stock setting 0x3A), not while PTT is held, scanning or in a menu */
	if (gStock.raw[0x3A] && !ptt && !SCAN_Active() && !s_tscan && s_screen == SCREEN_MAIN) {
		uint16_t a = ADC_Read(ADC_CH_PA1, 1);
		VoxAction_t v = VOX_Tick(a == 0xFFFFu ? 0 : (uint8_t)(a >> 2), gStock.raw[0x02], gStock.raw[0x20],
		                         RADIO_State() == RADIO_TX, APP_TICK_MS);
		if (v == VOX_START && !tx_busy_locked()) {
			dw_end();
			RADIO_StartTx();
		} else if (v == VOX_STOP)
			RADIO_EndTx();
	}
#endif
	dw_tick(ptt || s_tscan || SCAN_Active() || s_entry_len || s_screen != SCREEN_MAIN);
	if (RADIO_State() == RADIO_RX_OPEN)
		light_wake();
	else
		light_tick();
	if (ptt)
		SCAN_Stop();
	else if (SCAN_Tick(RADIO_State() == RADIO_RX_OPEN) == SCAN_HOP)
		scan_hop();

	/* battery about once a second, never while transmitting (the voltage sags) */
	if (++s_batt_ticks >= 1000 / APP_TICK_MS && RADIO_State() != RADIO_TX) {
		s_batt_ticks = 0;
		BATTERY_Set(ADC_Read(ADC_CH_BATTERY, 8));
	}

	k = key_event();
	/* while transmitting, a held digit, * or # sends its DTMF pair (stock 0x01E5457E / 0x01E5072C) */
	if (RADIO_State() == RADIO_TX && DTMFSEQ_Busy())
		k = KEY_INVALID;                   /* keys wait while a PTT-ID is sent */
	else if (RADIO_State() == RADIO_TX) {
		int d = KEY_Digit(s_key_held);

		if (s_key_held == KEY_STAR)
			d = 14;
		else if (s_key_held == KEY_HASH)
			d = 15;
		if (d >= 0) {
			if (k != KEY_INVALID)
				RADIO_TxTone((uint8_t)d, gStock.raw[SET_DTMF_MON] & 1u);
			k = KEY_INVALID;
		} else if (RADIO_TxToneOn())
			RADIO_TxToneOff();
	}
	if (k != KEY_INVALID)                        /* a press lights the screen and still does its job */
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
	} else if (k != KEY_INVALID && gStock.raw[SET_KEY_LOCK]) {
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
	if (k != KEY_INVALID)
		dw_end();                          /* keys act on the active side */
#if SETTINGS_ENABLE_SAVE
	if (k != KEY_INVALID)
		save_soon();
	save_tick(ptt || s_peek || s_tscan || SCAN_Active() || s_entry_len);
#endif
	if (s_note_ms)
		s_note_ms = s_note_ms > APP_TICK_MS ? s_note_ms - APP_TICK_MS : 0;
	if (s_lamp_mode == 2 && (s_lamp_ms += APP_TICK_MS) >= LAMP_STROBE_MS) {
		s_lamp_ms = 0;
		IO_Lamp(!IO_LampIsOn());
	}
	if (k != KEY_INVALID && !ptt) {
		if (gStock.raw[SET_BEEP])
			RADIO_Beep(1000, 40);
		if (s_screen == SCREEN_MAIN)
			key_main(k);
		else if (s_screen == SCREEN_ABOUT)
			go(SCREEN_MENU);
		else
			key_menu(k);
	}

	if (s_screen == SCREEN_MAIN) {
		draw_main();
	} else if (s_redraw && s_screen == SCREEN_ABOUT)
		draw_about();
	else if (s_redraw)
		draw_menu();
	s_redraw = false;
}
