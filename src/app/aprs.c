/* Open-UV5R-Mini: APRS (ENABLE_APP_APRS, Sean asked 2026-11-27): MENU -> APPS -> APRS.
 *
 * While open, the selected side listens on the region's APRS frequency (144.390 MHz in the US, 144.800 in Europe,
 * 145.175 in Australia); the VFO is put back when it closes, and nothing is saved meanwhile (APP_HoldSaves).
 *
 *   Receive   through the BK4819's FSK modem (radio/fskrx.c, experimental, not tried on a radio yet). Heard
 *             stations are listed newest first: call, kind, distance (when both positions are known). The line under
 *             the title shows how far the receiver gets: sync words found, good frames, bad checksums.
 *   Send      1 sends our position report now (radio/aprs.c, AFSK from the BK4819's tone generator, radio.c
 *             RADIO_SendAfsk), through the normal transmit checks (TX lock: only where the radio may transmit). With
 *             BEACON set, it is also sent every 10, 20 or 30 minutes while the app is open.
 *   Settings  * : call sign (keypad letters, 2 = A B C 2 ...), SSID, icon, latitude and longitude (typed DDMM.m, *
 *             flips N/S or E/W), the beacon time and the receive mode. Kept with the radio's settings.
 *
 * Keys on the list: UP/DOWN pick, MENU the station's details, 1 beacon, 0 the other receive mode, * settings,
 * EXIT back. */
#include "app/apps.h"
#if APP_APRS_ENABLE
#include "app/app.h"
#include "app/keys.h"
#include "app/monitor.h"
#include "driver/bk4819.h"
#include "driver/st7735s.h"
#include "radio/aprs.h"
#include "radio/ax25.h"
#include "radio/fskrx.h"
#include "radio/radio.h"
#include "radio/txlock.h"
#include "ui/helper.h"
#include "ui/status.h"
#include "ui/theme.h"

#define HEARD_N      8u
#define ROW_Y(r)     ((uint8_t)(26u + 11u * (r)))
#define SAY_MS       2500u
#define DUP_MS       30000u              /* the same packet again (digipeated) within this: counted, not listed */
#define TAP_MS       1000u               /* the same key again within this: the next letter on it */
#define PRE_FLAGS    36u                 /* 240 ms of flags before the frame */

typedef struct {
	AprsHeard_t h;
	uint32_t    t;                       /* gAppsUptimeMs when last heard */
	uint8_t     count;
} Heard_t;

enum { V_LIST, V_DETAIL, V_SET, V_EDIT };
enum { S_CALL, S_SSID, S_ICON, S_LAT, S_LON, S_BEACON, S_MODE, S_N };

static AprsConf_t s_c;
static Vfo_t      s_vfo_was;
static Heard_t    s_heard[HEARD_N];
static uint8_t    s_n, s_sel, s_view, s_row, s_mode;
static bool       s_redraw;
static uint8_t    s_drawn = 0xFF;        /* the view drawn last: another one needs the whole screen */
static char       s_say[22];
static uint16_t   s_say_ms, s_stat_ms;
static uint32_t   s_beacon_ms;           /* until the next timed beacon */
/* editing a text row */
static char       s_ed[8];
static uint8_t    s_ed_n;
static bool       s_ed_neg;
static int8_t     s_tap_key = -1;
static uint8_t    s_tap_i;
static uint32_t   s_tap_t;

static uint32_t aprs_freq(void)          /* 10 Hz units */
{
	switch (TXLOCK_Region()) {
	case REGION_US: return 14439000u;
	case REGION_AU: return 14517500u;
	default:        return 14480000u;
	}
}

static bool miles(void) { return TXLOCK_Region() == REGION_US; }

static uint32_t every_ms(void) { return (uint32_t)s_c.every * 600000u; }

static bool have_pos(void) { return s_c.lat || s_c.lon; }

static bool for_me(const AprsHeard_t *h)  /* kind "MSG ME" */
{
	return h->kind[0] == 'M' && h->kind[1] == 'S' && h->kind[4] == 'M';
}

static void say(const char *t)
{
	unsigned i = 0;
	while (t[i] && i < sizeof s_say - 1u) {
		s_say[i] = t[i];
		i++;
	}
	s_say[i] = 0;
	s_say_ms = SAY_MS;
	s_redraw = true;
}

static char *put(char *o, const char *s) { while (*s) *o++ = *s++; *o = 0; return o; }

static char *pad(char *o, char *start, unsigned w) { while ((unsigned)(o - start) < w) *o++ = ' '; *o = 0; return o; }

/* "12.3" / "123" tenths of km or mi */
static char *dist_text(char *o, uint32_t d)
{
	if (d < 1000u) {
		o = APPS_PutInt(o, (int)(d / 10u));
		*o++ = '.';
		*o++ = (char)('0' + d % 10u);
		*o = 0;
		return o;
	}
	return APPS_PutInt(o, (int)(d / 10u > 9999u ? 9999u : d / 10u));
}

static char *age_text(char *o, uint32_t ms)
{
	uint32_t s = ms / 1000u;
	if (s < 60u) { o = APPS_PutInt(o, (int)s); *o++ = 'S'; }
	else if (s < 3600u) { o = APPS_PutInt(o, (int)(s / 60u)); *o++ = 'M'; }
	else { o = APPS_PutInt(o, (int)(s / 3600u)); *o++ = 'H'; }
	*o = 0;
	return o;
}

/* ------------------------------------------------------------------ drawing */
static void line(uint8_t y, const char *t, uint16_t fg)
{
	UI_PrintPadded(t, 2, y, fg, COLOR_BG, 21);
}

static void bottom(const char *hint)
{
	line(118, s_say_ms ? s_say : hint, s_say_ms ? COLOR_WARN : COLOR_DIM);
}

static void stat_line(void)
{
	const FskRxStat_t *st = FSKRX_Stat();
	char b[24], *o = b;
	o = put(o, "SYN ");
	o = APPS_PutInt(o, st->syncs);
	o = put(o, " OK ");
	o = APPS_PutInt(o, st->frames);
	o = put(o, " BAD ");
	o = APPS_PutInt(o, st->bad);
	o = put(o, s_mode ? " M2" : " M1");
	line(15, b, COLOR_DIM);
}

static void title(void)
{
	char f[12];
	APPS_FormatMhz(f, aprs_freq());
	UI_TitleBar(s_view == V_SET || s_view == V_EDIT ? "APRS SET" : "APRS", f);
}

static void draw_list(void)
{
	stat_line();
	for (unsigned r = 0; r < HEARD_N; r++) {
		char b[24], *o = b;
		if (r < s_n) {
			const Heard_t *e = &s_heard[r];
			unsigned k;
			for (k = 0; e->h.from[k] && k < 9u; k++)
				*o++ = e->h.from[k];
			o = pad(o, b, 10);
			o = put(o, e->h.kind);
			o = pad(o, b, 17);
			if (e->h.has_pos && have_pos())
				dist_text(o, APRS_Distance(s_c.lat, s_c.lon, e->h.lat, e->h.lon, miles(), 0));
			else
				age_text(o, gAppsUptimeMs - e->t);
		} else
			b[0] = 0;
		if (r == 0 && !s_n)
			put(b, "LISTENING...");
		line(ROW_Y(r), b, r < s_n && r == s_sel ? COLOR_ACCENT :
		                  r < s_n && for_me(&s_heard[r].h) ? COLOR_WARN : COLOR_TEXT);
	}
	bottom(s_c.call[0] ? "1 SEND  * SET  MENU" : "* SET YOUR CALL");
}

static void draw_detail(void)
{
	const Heard_t *e = &s_heard[s_sel];
	char b[24], *o;
	const char *t = e->h.text;

	o = put(b, e->h.from);
	*o++ = ' ';
	o = age_text(o, gAppsUptimeMs - e->t);
	o = put(o, " AGO");
	if (e->count > 1u) {
		o = put(o, " X");
		APPS_PutInt(o, e->count);
	}
	line(15, b, COLOR_TEXT);
	o = put(b, e->h.kind);
	if (e->h.sym) {
		o = put(o, " ICON ");
		*o++ = e->h.sym_table;
		*o++ = e->h.sym;
		*o = 0;
	}
	line(ROW_Y(0), b, COLOR_INFO);
	if (e->h.has_pos) {
		o = put(b, "LAT ");
		APRS_LatText(o, e->h.lat);
		line(ROW_Y(1), b, COLOR_TEXT);
		o = put(b, "LON ");
		APRS_LonText(o, e->h.lon);
		line(ROW_Y(2), b, COLOR_TEXT);
		if (have_pos()) {
			uint16_t brg;
			uint32_t d = APRS_Distance(s_c.lat, s_c.lon, e->h.lat, e->h.lon, miles(), &brg);
			o = dist_text(b, d);
			o = put(o, miles() ? " MI  BRG " : " KM  BRG ");
			APPS_PutInt(o, brg);
			line(ROW_Y(3), b, COLOR_TEXT);
		} else
			line(ROW_Y(3), "", COLOR_TEXT);
	} else
		for (unsigned r = 1; r < 4; r++)
			line(ROW_Y(r), "", COLOR_TEXT);
	for (unsigned r = 4; r < 8; r++) {   /* the text, 21 characters a line */
		unsigned k = 0;
		while (*t && k < 21u)
			b[k++] = *t++;
		b[k] = 0;
		line(ROW_Y(r), b, COLOR_DIM);
	}
	bottom("EXIT BACK  UP/DN NEXT");
}

static const char *const MODE_NAME[2] = { "1200/1800", "1200/2400" };
static const char *const EVERY_NAME[4] = { "OFF", "10 MIN", "20 MIN", "30 MIN" };

static void set_value(char *o, unsigned row)
{
	switch (row) {
	case S_CALL:   put(o, s_c.call[0] ? s_c.call : "NOT SET"); break;
	case S_SSID:   APPS_PutInt(o, s_c.ssid); break;
	case S_ICON:   put(o, APRS_IconName(s_c.icon)); break;
	case S_LAT:    APRS_LatText(o, s_c.lat); break;
	case S_LON:    APRS_LonText(o, s_c.lon); break;
	case S_BEACON: put(o, EVERY_NAME[s_c.every & 3u]); break;
	default:       put(o, MODE_NAME[s_mode]); break;
	}
}

/* the row being typed: "47 39.5_ N" */
static void edit_value(char *o, unsigned row)
{
	if (row == S_CALL) {
		for (unsigned k = 0; k < s_ed_n; k++)
			*o++ = s_ed[k];
		*o++ = '_';
		*o = 0;
		return;
	}
	{
		unsigned dd = row == S_LAT ? 2u : 3u;
		for (unsigned k = 0; k < dd + 3u; k++) {
			if (k == dd || k == dd + 2u)
				*o++ = k == dd ? ' ' : '.';
			*o++ = k < s_ed_n ? s_ed[k] : '_';
		}
		*o++ = ' ';
		*o++ = row == S_LAT ? (s_ed_neg ? 'S' : 'N') : (s_ed_neg ? 'W' : 'E');
		*o = 0;
	}
}

static void draw_set(void)
{
	static const char *const LABEL[S_N] = { "CALL", "SSID", "ICON", "LAT", "LON", "BEACON", "RX MODE" };
	line(15, "", COLOR_DIM);
	for (unsigned r = 0; r < S_N; r++) {
		char b[24], *o = put(b, LABEL[r]);
		o = pad(o, b, 8);
		if (s_view == V_EDIT && r == s_row)
			edit_value(o, r);
		else
			set_value(o, r);
		line(ROW_Y(r), b, r == s_row ? (s_view == V_EDIT ? COLOR_WARN : COLOR_ACCENT) : COLOR_TEXT);
	}
	line(ROW_Y(7), "", COLOR_DIM);
	if (s_view == V_EDIT)
		bottom(s_row == S_CALL ? "KEYS ABC  * DEL  MENU" : "DIGITS  * N/S E/W MENU");
	else
		bottom("MENU CHANGE  EXIT DONE");
}

static void draw(bool full)
{
	if (full) {                           /* the screen it opens on (the menu) is still there: clear it */
		ST7735S_FillRect(0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_BG);
		title();
	}
	if (s_view == V_LIST)
		draw_list();
	else if (s_view == V_DETAIL)
		draw_detail();
	else
		draw_set();
}

/* ------------------------------------------------------------------ sending */
static void send_beacon(void)
{
	uint8_t f[128];
	unsigned n = APRS_BuildBeacon(&s_c, f, sizeof f);

	s_beacon_ms = every_ms();
	if (!n) {
		say("SET YOUR CALL (*)");
		return;
	}
	FSKRX_Stop();
	if (!RADIO_StartTx()) {
		const char *why = RADIO_TxRefusal();
		say(why ? why : "TX REFUSED");
		FSKRX_Start(s_mode);
		return;
	}
	line(118, "SENDING...", COLOR_TX);
	RADIO_SendAfsk(f, n, PRE_FLAGS);
	RADIO_EndTx();
	FSKRX_Start(s_mode);
	say("BEACON SENT");
}

/* ------------------------------------------------------------------ receiving */
static void heard(const uint8_t *frame, unsigned len)
{
	static char text[200];
	char me[10];
	AprsHeard_t h;

	if (!AX25_ToText(frame, len, text, sizeof text))
		return;
	MON_Event("aprs", text);
	APRS_CallText(me, &s_c);
	if (!APRS_Describe(text, me, &h))
		return;
	for (unsigned i = 0; i < s_n; i++) {  /* heard before: to the top */
		Heard_t e = s_heard[i];
		bool same = true;
		for (unsigned k = 0; same && (e.h.from[k] || h.from[k]); k++)
			same = e.h.from[k] == h.from[k];
		if (!same)
			continue;
		if (gAppsUptimeMs - e.t < DUP_MS && e.count < 255u)
			e.count++;
		else
			e.count = 1;
		e.h = h;
		e.t = gAppsUptimeMs;
		for (unsigned j = i; j; j--)
			s_heard[j] = s_heard[j - 1];
		s_heard[0] = e;
		s_redraw = true;
		return;
	}
	for (unsigned j = (s_n < HEARD_N ? s_n : HEARD_N - 1u); j; j--)
		s_heard[j] = s_heard[j - 1];
	s_heard[0].h = h;
	s_heard[0].t = gAppsUptimeMs;
	s_heard[0].count = 1;
	if (s_n < HEARD_N)
		s_n++;
	if (s_view == V_LIST)
		s_sel = 0;
	RADIO_Beep(for_me(&h) ? 1500 : 1000, 60);
	if (for_me(&h))
		say("MESSAGE FOR YOU");
	s_redraw = true;
}

/* ------------------------------------------------------------------ editing */
static const char *const TAPS[10] = { "0", "1", "ABC2", "DEF3", "GHI4", "JKL5", "MNO6", "PQRS7", "TUV8", "WXYZ9" };

static void edit_start(void)
{
	s_view = V_EDIT;
	s_ed_n = 0;
	s_tap_key = -1;
	if (s_row == S_CALL)
		for (; s_c.call[s_ed_n] && s_ed_n < 6u; s_ed_n++)
			s_ed[s_ed_n] = s_c.call[s_ed_n];
	else
		s_ed_neg = (s_row == S_LAT ? s_c.lat : s_c.lon) < 0;
}

static void edit_done(void)
{
	if (s_row == S_CALL) {
		char cs[7];
		uint8_t ss;
		s_ed[s_ed_n] = 0;
		if (!s_ed_n)
			s_c.call[0] = 0;
		else if (AX25_ParseCall(s_ed, cs, &ss))
			for (unsigned k = 0; k < 7u; k++)
				s_c.call[k] = cs[k];
		else {
			say("NOT A CALL SIGN");
			return;
		}
	} else {
		unsigned dd = s_row == S_LAT ? 2u : 3u;
		uint32_t deg = 0, min10 = 0;
		int32_t v;
		if (s_ed_n < dd + 3u) {
			say("TYPE ALL DIGITS");
			return;
		}
		for (unsigned k = 0; k < dd; k++)
			deg = deg * 10u + (uint32_t)(s_ed[k] - '0');
		for (unsigned k = dd; k < dd + 3u; k++)
			min10 = min10 * 10u + (uint32_t)(s_ed[k] - '0');
		if (min10 >= 600u || deg > (s_row == S_LAT ? 89u : 179u)) {
			say("OUT OF RANGE");
			return;
		}
		v = (int32_t)(deg * 6000u + min10 * 10u);
		if (s_ed_neg)
			v = -v;
		if (s_row == S_LAT)
			s_c.lat = v;
		else
			s_c.lon = v;
	}
	s_view = V_SET;
}

static void edit_key(KEY_Code_t k)
{
	int d = KEY_Digit(k);

	if (k == KEY_ACT_EXIT) {
		s_view = V_SET;
	} else if (k == KEY_ACT_MENU) {
		edit_done();
	} else if (k == KEY_STAR) {
		if (s_row == S_CALL) {
			if (s_ed_n)
				s_ed_n--;
			s_tap_key = -1;
		} else
			s_ed_neg = !s_ed_neg;
	} else if (d >= 0 && s_row == S_CALL) {
		const char *g = TAPS[d];
		uint32_t now = gAppsUptimeMs;
		if (d == s_tap_key && now - s_tap_t < TAP_MS && s_ed_n) {
			s_tap_i = (uint8_t)((s_tap_i + 1u) % (unsigned)(g[1] ? (g[2] ? (g[3] ? (g[4] ? 5 : 4) : 3) : 2) : 1));
			s_ed[s_ed_n - 1u] = g[s_tap_i];
		} else if (s_ed_n < 6u) {
			s_tap_i = 0;
			s_ed[s_ed_n++] = g[0];
		}
		s_tap_key = (int8_t)d;
		s_tap_t = now;
	} else if (d >= 0) {
		unsigned dd = s_row == S_LAT ? 2u : 3u;
		if (s_ed_n < dd + 3u)
			s_ed[s_ed_n++] = (char)('0' + d);
	}
	s_redraw = true;
}

/* ------------------------------------------------------------------ the cable monitor (debug builds) */
#if DEBUGGING
static char *ph(char *o, uint32_t v, unsigned d)
{
	for (unsigned k = d; k; k--) {
		unsigned x = v >> (4u * (k - 1u)) & 15u;
		*o++ = (char)(x < 10u ? '0' + x : 'A' + x - 10u);
	}
	*o = 0;
	return o;
}

static bool mword(const char **s, const char *w)
{
	const char *p = *s;
	while (*w)
		if (*p++ != *w++)
			return false;
	if (*p && *p != ' ')
		return false;
	while (*p == ' ')
		p++;
	*s = p;
	return true;
}

static uint32_t mhex(const char **s)
{
	uint32_t v = 0;
	const char *p = *s;
	for (;; p++) {
		char c = *p;
		if (c >= '0' && c <= '9') v = v << 4 | (uint32_t)(c - '0');
		else if (c >= 'A' && c <= 'F') v = v << 4 | (uint32_t)(c - 'A' + 10);
		else if (c >= 'a' && c <= 'f') v = v << 4 | (uint32_t)(c - 'a' + 10);
		else break;
	}
	while (*p == ' ')
		p++;
	*s = p;
	return v;
}

static bool mon(const char *a)
{
	char b[120], *o;
	const FskRxStat_t *st = FSKRX_Stat();

	if (mword(&a, "stat")) {
		o = put(b, "@syncs ");
		o = APPS_PutInt(o, st->syncs);
		o = put(o, " done ");
		o = APPS_PutInt(o, st->done);
		o = put(o, " words ");
		o = APPS_PutInt(o, st->words);
		o = put(o, " frames ");
		o = APPS_PutInt(o, st->frames);
		o = put(o, " bad ");
		o = APPS_PutInt(o, st->bad);
		o = put(o, " mode ");
		o = APPS_PutInt(o, st->mode);
		o = put(o, " order ");
		o = APPS_PutInt(o, st->order == 0xFF ? -1 : st->order);
		o = put(o, FSKRX_Inverted() ? " sync 0101" : " sync FEFE");
		MON_Line(b);
		o = put(b, "@regs");
		{
			static const uint8_t R[] = { 0x02, 0x0C, 0x3F, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x70, 0x72 };
			for (unsigned i = 0; i < sizeof R; i++) {
				*o++ = ' ';
				o = ph(o, R[i], 2);
				*o++ = '=';
				o = ph(o, BK4819_ReadRegister(R[i]), 4);
			}
		}
		MON_Line(b);
		return true;
	}
	if (mword(&a, "raw")) {
		static uint16_t w[FSKRX_RAW];
		unsigned n = FSKRX_Raw(w);
		for (unsigned i = 0; i < n; ) {
			o = put(b, "@");
			for (unsigned k = 0; k < 16u && i < n; k++, i++) {
				o = ph(o, w[i], 4);
				*o++ = ' ';
			}
			*o = 0;
			MON_Line(b);
		}
		return true;
	}
	if (mword(&a, "mode")) {
		s_mode = (uint8_t)(mhex(&a) & 1u);
		FSKRX_Start(s_mode);
		s_redraw = true;
		return true;
	}
	if (mword(&a, "sync")) {             /* sync HHHH LLLL, or sync 0 0 = the flags, taking turns */
		uint16_t hi = (uint16_t)mhex(&a);
		FSKRX_SetSync(hi, (uint16_t)mhex(&a));
		return true;
	}
	if (mword(&a, "restart")) {
		FSKRX_Start(s_mode);
		return true;
	}
	if (mword(&a, "beacon")) {
		send_beacon();
		MON_Line(s_say);
		return true;
	}
	if (mword(&a, "conf")) {
		o = put(b, "@call ");
		o = APRS_CallText(o, &s_c);
		o = put(o, " lat ");
		o = APRS_LatText(o, s_c.lat);
		o = put(o, " lon ");
		o = APRS_LonText(o, s_c.lon);
		o = put(o, " icon ");
		o = put(o, APRS_IconName(s_c.icon));
		o = put(o, " every ");
		APPS_PutInt(o, s_c.every * 10);
		MON_Line(b);
		return true;
	}
	if (mword(&a, "heard")) {            /* heard <tnc2 text>: as if received (tests the list without a signal) */
		uint8_t f[AX25_MAX_FRAME];
		char src[10], dst[8], *p = src;
		const char *t = a, *info;
		unsigned n;
		while (*t && *t != '>' && p < src + 9)
			*p++ = *t++;
		*p = 0;
		if (*t++ != '>')
			return false;
		for (p = dst; *t && *t != ':' && *t != ',' && p < dst + 7; )
			*p++ = *t++;
		*p = 0;
		for (info = t; *info && *info != ':'; info++)
			;
		if (!*info)
			return false;
		info++;
		{
			unsigned il = 0;
			while (info[il])
				il++;
			n = AX25_BuildUi(f, sizeof f, dst, src, "", (const uint8_t *)info, il);
		}
		if (!n)
			return false;
		heard(f, n - 2u);
		return true;
	}
	MON_Line("@app: stat raw mode N sync HHHH LLLL restart beacon conf heard TNC2");
	return mword(&a, "help");
}
#endif

/* ------------------------------------------------------------------ the app */
static void enter(void)
{
	uint8_t p[APRS_PACK_LEN];

	APP_AprsConf(p);
	APRS_Unpack(p, &s_c);
	s_vfo_was = gVfo;
	APP_HoldSaves(true);
	gVfo.freq = aprs_freq();
	gVfo.tx_freq = 0;
	gVfo.rx_only = false;
	gVfo.rx_tone = gVfo.tx_tone = 0;
	gVfo.narrow = false;
	gVfo.mod = MOD_FM;
	gVfo.ptt_id = 0;
	RADIO_Apply();
	FSKRX_Start(s_mode);
#if DEBUGGING
	gMonApp = mon;
#endif
	s_view = V_LIST;
	s_sel = 0;
	s_say_ms = 0;
	s_beacon_ms = every_ms();
	s_redraw = true;
	s_drawn = 0xFF;
}

static void leave(void)
{
	uint8_t p[APRS_PACK_LEN];

	FSKRX_Stop();
#if DEBUGGING
	gMonApp = 0;
#endif
	gVfo = s_vfo_was;
	RADIO_Apply();
	APP_HoldSaves(false);
	APRS_Pack(&s_c, p);
	APP_SetAprsConf(p);
}

static AppsResult_t key(KEY_Code_t k)
{
	if (s_view == V_EDIT) {
		edit_key(k);
		return APPS_STAY;
	}
	if (s_view == V_SET) {
		if (k == KEY_ACT_EXIT) {
			uint8_t p[APRS_PACK_LEN];
			APRS_Pack(&s_c, p);
			APRS_Unpack(p, &s_c);             /* as it will be kept (1/10 minute) */
			s_beacon_ms = every_ms();
			s_view = V_LIST;
		} else if (k == KEY_ACT_UP)
			s_row = (uint8_t)((s_row + S_N - 1u) % S_N);
		else if (k == KEY_ACT_DOWN)
			s_row = (uint8_t)((s_row + 1u) % S_N);
		else if (k == KEY_ACT_MENU) {
			switch (s_row) {
			case S_SSID:   s_c.ssid = (uint8_t)((s_c.ssid + 1u) & 15u); break;
			case S_ICON:   s_c.icon = (uint8_t)((s_c.icon + 1u) % APRS_ICONS); break;
			case S_BEACON: s_c.every = (uint8_t)((s_c.every + 1u) & 3u); break;
			case S_MODE:   s_mode ^= 1u; FSKRX_Start(s_mode); break;
			default:       edit_start(); break;
			}
		}
		s_redraw = true;
		return APPS_STAY;
	}
	if (s_view == V_DETAIL) {
		if (k == KEY_ACT_EXIT || k == KEY_ACT_MENU)
			s_view = V_LIST;
		else if (k == KEY_ACT_UP && s_sel)
			s_sel--;
		else if (k == KEY_ACT_DOWN && s_sel + 1u < s_n)
			s_sel++;
		s_redraw = true;
		return APPS_STAY;
	}
	if (k == KEY_ACT_EXIT)
		return APPS_EXIT;
	if (k == KEY_ACT_UP && s_sel)
		s_sel--;
	else if (k == KEY_ACT_DOWN && s_sel + 1u < s_n)
		s_sel++;
	else if (k == KEY_ACT_MENU && s_n)
		s_view = V_DETAIL;
	else if (k == KEY_STAR) {
		s_view = V_SET;
		s_row = 0;
	} else if (k == KEY_1)
		send_beacon();
	else if (k == KEY_0) {
		s_mode ^= 1u;
		FSKRX_Start(s_mode);
		say(s_mode ? "RX FFSK 1200/2400" : "RX FFSK 1200/1800");
	}
	s_redraw = true;
	return APPS_STAY;
}

static AppsResult_t tick(bool full)
{
	const uint8_t *f;
	unsigned n = FSKRX_Poll(&f);

	if (n)
		heard(f, n);
#if DEBUGGING
	{
		static uint16_t syn;
		if (FSKRX_Stat()->syncs != syn) {
			char b[12];
			syn = FSKRX_Stat()->syncs;
			APPS_PutInt(b, syn);
			MON_Event("fsk sync", b);
		}
	}
#endif
	if (s_say_ms && (s_say_ms = s_say_ms > APP_TICK_MS ? (uint16_t)(s_say_ms - APP_TICK_MS) : 0) == 0)
		s_redraw = true;
	if ((s_stat_ms = (uint16_t)(s_stat_ms + APP_TICK_MS)) >= 1000u) {   /* counts and ages */
		s_stat_ms = 0;
		if (s_view == V_LIST)
			s_redraw = true;
	}
	if (s_c.every && s_c.call[0] && s_view == V_LIST) {
		if (s_beacon_ms > APP_TICK_MS)
			s_beacon_ms -= APP_TICK_MS;
		else
			send_beacon();
	}
	if (full || s_redraw || s_drawn != s_view) {
		draw(full || (s_drawn >= V_SET) != (s_view >= V_SET));   /* the title: APRS or APRS SET */
		s_drawn = s_view;
		s_redraw = false;
	}
	return APPS_STAY;
}

const AppDef_t APP_APRS = { "APRS", enter, key, tick, leave, false, false,
	"UP/DN  PICK A STATION\nMENU   ITS DETAILS\n1      SEND POSITION\n0      OTHER RX MODE\n*      SETTINGS\n"
	"EXIT   BACK\n\nSET: MENU CHANGES ROW\nCALL: 2=ABC2 ... * DEL\nLAT/LON: * FLIPS N/S" };
#endif
