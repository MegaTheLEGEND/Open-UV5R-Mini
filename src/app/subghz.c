/* Open-UV5R-Mini: SUB-GHZ (ENABLE_APP_SUBGHZ, Sean asked 2026-11-27): MENU -> APPS -> SUB-GHZ. Receive only.
 *
 * Records the on/off pulses of cheap remote controls (doorbells, plug sockets, gates: OOK at 315 or 433.92 MHz)
 * from the BK4819's signal strength (REG_67), read as fast as the bit-banged bus goes, and finds their code
 * (radio/ook.c: PT2262 / EV1527, "Princeton" in Flipper Zero's terms). The BK4819 has no OOK mode: how fast its
 * RSSI follows a pulse is what the first tests on a radio show (the cable monitor's "!app trace").
 *
 * The capture lives in the update loader's buffer (16 KB, free outside the power-on update mode): 4096 durations.
 * Nothing is written to flash; the cable monitor hands it to the PC as a Flipper .sub file (tools/subget.py).
 *
 * Keys: 1 capture (3 s: press the remote meanwhile), 5 find (hold the remote: 3 s sweep of +-500 kHz, then tunes to
 * the strongest), UP/DOWN 25 kHz, 0 the next preset frequency, EXIT back. While open, the selected side is on the
 * frequency shown, in AM (the remote's buzz can be heard); the VFO is put back on leaving. */
#include "app/apps.h"
#if APP_SUBGHZ_ENABLE
#include "app/app.h"
#include "app/keys.h"
#include "app/monitor.h"
#include "driver/bk4819.h"
#include "driver/st7735s.h"
#include "driver/system.h"
#include "radio/ook.h"
#include "radio/radio.h"
#include "ui/helper.h"
#include "ui/status.h"
#include "ui/theme.h"
#include "update/update.h"

#define CAPTURE_MS 3000u
#define FIND_MS    3000u
#define FIND_SPAN  50000u                /* +-500 kHz, 10 Hz units */
#define FIND_STEP  2500u                 /* 25 kHz */
#define ROW_Y(r)   ((uint8_t)(15u + 11u * (r)))

static const uint32_t PRESETS[] = { 43392000u, 31500000u, 43342000u, 43442000u, 39000000u, 41800000u };
#define NPRESET (sizeof PRESETS / sizeof PRESETS[0])

static Vfo_t     s_vfo_was;
static uint8_t   s_preset;
static int32_t  *s_d;                    /* the capture: us, + carrier on, - off */
static unsigned  s_max, s_n;
static uint32_t  s_samples, s_us;        /* RSSI reads in the last capture, and its length */
static uint16_t  s_floor, s_thr_hi, s_thr_lo, s_peak;
static uint8_t   s_margin = 20;          /* threshold above the noise floor, raw units (0.5 dB) */
/* what the capture reads (debug: "!app src"): REG_67 (RSSI) is smoothed over ~10 ms on the BK4819 (Sean's radio,
 * 2026-11-27), too slow for 265 us pulses, so other registers can be tried; inverted = the value falls with the
 * carrier (noise) */
static uint8_t   s_reg = 0x67;
static uint16_t  s_mask = 0x01FF;
static bool      s_inv;

static uint16_t src_read(void)
{
	uint16_t v = BK4819_ReadRegister(s_reg) & s_mask;
	return s_inv ? (uint16_t)(s_mask - v) : v;
}
static OokCode_t s_code;
static bool      s_redraw;
static uint16_t  s_ms;
static char      s_msg[22];

static char *put(char *o, const char *s) { while (*s) *o++ = *s++; *o = 0; return o; }

static char *phex(char *o, uint32_t v, unsigned d)
{
	for (unsigned k = d; k; k--) {
		unsigned x = v >> (4u * (k - 1u)) & 15u;
		*o++ = (char)(x < 10u ? '0' + x : 'A' + x - 10u);
	}
	*o = 0;
	return o;
}

static int dbm(uint16_t raw) { return raw ? (int)(raw / 2u) - 160 : -160; }

static void line(uint8_t y, const char *t, uint16_t fg) { UI_PrintPadded(t, 2, y, fg, COLOR_BG, 21); }

static void msg(const char *t)
{
	unsigned i = 0;
	for (; t[i] && i < sizeof s_msg - 1u; i++)
		s_msg[i] = t[i];
	s_msg[i] = 0;
	s_redraw = true;
}

static void tune(uint32_t f)
{
	gVfo.freq = f;
	RADIO_Apply();
	s_redraw = true;
}

/* ------------------------------------------------------------------ capture */
static uint32_t ticks_to_us(uint32_t t)
{
	uint32_t tps = SYSTEM_TicksPerSec();
	return t / (tps / 1000u) * 1000u + t % (tps / 1000u) * 1000u / (tps / 1000u);
}

static void noise_floor(void)
{
	uint32_t sum = 0;
	for (unsigned i = 0; i < 64u; i++)
		sum += src_read();
	s_floor = (uint16_t)(sum / 64u);
	s_thr_hi = (uint16_t)(s_floor + s_margin);
	s_thr_lo = (uint16_t)(s_floor + s_margin * 3u / 5u);
}

static void capture(uint32_t ms)
{
	uint32_t t0, last, t, end;
	bool level = false, started = false;

	line(ROW_Y(9), "PRESS THE REMOTE NOW", COLOR_WARN);
	noise_floor();
	s_n = 0;
	s_samples = 0;
	s_peak = 0;
	end = ms * (SYSTEM_TicksPerSec() / 1000u);
	t0 = last = SYSTEM_Raw();
	do {
		uint16_t r = src_read();
		bool nl = r >= s_thr_hi ? true : r <= s_thr_lo ? false : level;
		t = SYSTEM_Raw();
		if (r > s_peak)
			s_peak = r;
		if (nl != level) {
			if (started) {
				int32_t us = (int32_t)ticks_to_us(t - last);
				s_d[s_n++] = level ? us : -us;
			} else
				started = nl;
			level = nl;
			last = t;
		}
		if (!(++s_samples & 255u))
			SYSTEM_WatchdogClear();
	} while (t - t0 < end && s_n + 1u < s_max);
	if (started && s_n < s_max)
		s_d[s_n++] = level ? (int32_t)ticks_to_us(t - last) : -(int32_t)ticks_to_us(t - last);
	s_us = ticks_to_us(t - t0);
	OOK_Princeton(s_d, s_n, &s_code);
	msg(s_code.ok ? "CODE FOUND" : s_n ? "NO KNOWN CODE (RAW)" : "NOTHING HEARD");
	MON_Event("subghz", s_code.ok ? "code" : "capture");
}

/* the strongest frequency within +-500 kHz while the remote is held */
static void find(void)
{
	uint16_t *best = (uint16_t *)s_d;     /* the capture's buffer (RAM is short): the capture goes */
	const unsigned np = 2u * FIND_SPAN / FIND_STEP + 1u;
	uint32_t c = gVfo.freq, t0 = SYSTEM_Ms();
	unsigned bi = np / 2u;

	line(ROW_Y(9), "HOLD THE REMOTE...", COLOR_WARN);
	for (unsigned i = 0; i < np; i++)
		best[i] = 0;
	RADIO_ProbeBegin();
	while (SYSTEM_Ms() - t0 < FIND_MS) {
		for (unsigned i = 0; i < np; i++) {
			uint16_t r = RADIO_ProbeRssi(c - FIND_SPAN + i * FIND_STEP, 500);
			if (r > best[i])
				best[i] = r;
		}
		SYSTEM_WatchdogClear();
	}
	RADIO_ProbeEnd();
	for (unsigned i = 0; i < np; i++)
		if (best[i] > best[bi])
			bi = i;
	tune(c - FIND_SPAN + bi * FIND_STEP);
	s_n = 0;
	s_code.ok = false;
	{
		char b[22], *o = put(b, "PEAK ");
		o = APPS_PutInt(o, dbm(best[bi]));
		put(o, " DBM");
		msg(b);
	}
}

/* ------------------------------------------------------------------ drawing */
static void draw(bool full)
{
	char b[24], *o;

	if (full) {
		char f[12];
		ST7735S_FillRect(0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_BG);
		APPS_FormatMhz(f, gVfo.freq);
		UI_TitleBar("SUB-GHZ", f);
	} else {
		char f[12];
		APPS_FormatMhz(f, gVfo.freq);
		UI_TitleRight(f, COLOR_DIM);
	}
	o = put(b, "RSSI ");
	o = APPS_PutInt(o, dbm(BK4819_GetRSSI()));
	o = put(o, " DBM  AM RX");
	line(ROW_Y(0), b, COLOR_INFO);
	if (s_n) {
		o = put(b, "EDGES ");
		o = APPS_PutInt(o, (int)s_n);
		o = put(o, "  ");
		o = APPS_PutInt(o, (int)(s_us / 1000u));
		put(o, " MS");
		line(ROW_Y(1), b, COLOR_TEXT);
		o = put(b, "READ ");
		o = APPS_PutInt(o, s_samples ? (int)(s_us / s_samples) : 0);
		o = put(o, " US  PEAK ");
		APPS_PutInt(o, dbm(s_peak));
		line(ROW_Y(2), b, COLOR_DIM);
	} else {
		line(ROW_Y(1), "NO CAPTURE YET", COLOR_DIM);
		line(ROW_Y(2), "", COLOR_DIM);
	}
	if (s_code.ok) {
		line(ROW_Y(3), "PRINCETON 24 BIT", COLOR_RX);
		o = phex(put(b, "KEY "), s_code.key, 6);
		o = put(o, " TE ");
		APPS_PutInt(o, s_code.te);
		line(ROW_Y(4), b, COLOR_TEXT);
		o = put(b, "FRAMES ");
		o = APPS_PutInt(o, s_code.frames);
		if (s_code.other) {
			o = put(o, " OTHER ");
			APPS_PutInt(o, s_code.other);
		}
		line(ROW_Y(5), b, COLOR_DIM);
	} else {
		line(ROW_Y(3), s_n ? "NO KNOWN CODE" : "", COLOR_DIM);
		line(ROW_Y(4), "", COLOR_TEXT);
		line(ROW_Y(5), "", COLOR_DIM);
	}
	line(ROW_Y(6), "1 CAPTURE  5 FIND", COLOR_DIM);
	line(ROW_Y(7), "0 PRESET  UP/DN 25K", COLOR_DIM);
	line(ROW_Y(9), s_msg, COLOR_WARN);
}

/* ------------------------------------------------------------------ the cable monitor (debug builds) */
#if DEBUGGING
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
		else if (c >= 'a' && c <= 'f') v = v << 4 | (uint32_t)(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F') v = v << 4 | (uint32_t)(c - 'A' + 10);
		else break;
	}
	while (*p == ' ')
		p++;
	*s = p;
	return v;
}

static uint32_t mdec(const char **s)
{
	uint32_t v = 0;
	const char *p = *s;
	while (*p >= '0' && *p <= '9')
		v = v * 10u + (uint32_t)(*p++ - '0');
	while (*p == ' ')
		p++;
	*s = p;
	return v;
}

/* durations, 16 to a line, with a prefix ("@" or "@RAW_Data: ") */
static void dump(const char *prefix, unsigned per)
{
	char b[200];
	for (unsigned i = 0; i < s_n; ) {
		char *o = put(b, prefix);
		for (unsigned k = 0; k < per && i < s_n; k++, i++) {
			o = APPS_PutInt(o, (int)s_d[i]);
			*o++ = ' ';
		}
		o[-1] = 0;
		MON_Line(b);
	}
}

static bool mon(const char *a)
{
	char b[120], *o;

	if (mword(&a, "stat")) {
		o = put(b, "@freq ");
		o = APPS_PutInt(o, (int)gVfo.freq);
		o = put(o, "0 edges ");
		o = APPS_PutInt(o, (int)s_n);
		o = put(o, " ms ");
		o = APPS_PutInt(o, (int)(s_us / 1000u));
		o = put(o, " reads ");
		o = APPS_PutInt(o, (int)s_samples);
		o = put(o, " floor ");
		o = APPS_PutInt(o, s_floor);
		o = put(o, " thr ");
		o = APPS_PutInt(o, s_thr_lo);
		*o++ = '/';
		o = APPS_PutInt(o, s_thr_hi);
		o = put(o, " peak ");
		o = APPS_PutInt(o, s_peak);
		o = put(o, " rssi ");
		APPS_PutInt(o, BK4819_GetRSSI());
		MON_Line(b);
		if (s_code.ok) {
			o = phex(put(b, "@princeton key "), s_code.key, 6);
			o = put(o, " te ");
			o = APPS_PutInt(o, s_code.te);
			o = put(o, " frames ");
			o = APPS_PutInt(o, s_code.frames);
			o = put(o, " other ");
			APPS_PutInt(o, s_code.other);
			MON_Line(b);
		}
		return true;
	}
	if (mword(&a, "cap")) {              /* cap [ms] */
		uint32_t ms = mdec(&a);
		capture(ms ? (ms > 10000u ? 10000u : ms) : CAPTURE_MS);
		s_redraw = true;
		return mon("stat");
	}
	if (mword(&a, "raw")) {
		dump("@", 16);
		return true;
	}
	if (mword(&a, "sub")) {              /* a Flipper .sub file; "sub raw" always RAW */
		bool raw = mword(&a, "raw") || !s_code.ok;
		MON_Line(raw ? "@Filetype: Flipper SubGhz RAW File" : "@Filetype: Flipper SubGhz Key File");
		MON_Line("@Version: 1");
		o = APPS_PutInt(put(b, "@Frequency: "), (int)gVfo.freq);
		put(o, "0");
		MON_Line(b);
		MON_Line("@Preset: FuriHalSubGhzPresetOok650Async");
		if (raw) {
			MON_Line("@Protocol: RAW");
			dump("@RAW_Data: ", 24);
		} else {
			MON_Line("@Protocol: Princeton");
			MON_Line("@Bit: 24");
			o = phex(put(b, "@Key: 00 00 00 00 00 "), s_code.key >> 16, 2);
			*o++ = ' ';
			o = phex(o, s_code.key >> 8 & 0xFFu, 2);
			*o++ = ' ';
			phex(o, s_code.key & 0xFFu, 2);
			MON_Line(b);
			APPS_PutInt(put(b, "@TE: "), s_code.te);
			MON_Line(b);
		}
		return true;
	}
	if (mword(&a, "src")) {              /* src REG MASK INV (hex, hex, 0/1): what the capture reads */
		uint32_t r = mhex(&a), m = mhex(&a), inv = mhex(&a);
		if (r > 0x7Fu || !m)
			return false;
		s_reg = (uint8_t)r;
		s_mask = (uint16_t)m;
		s_inv = inv != 0;
		return true;
	}
	if (mword(&a, "trace")) {            /* trace [ms] [REG hex]: every read with its time (us); default REG_67 */
		uint32_t ms = mdec(&a), t0, t, end, reg = *a ? mhex(&a) : 0x67u;
		unsigned n = 0, max = s_max / 2u;     /* pairs of 16-bit: time / 4 us, RSSI */
		uint16_t *p = (uint16_t *)s_d;
		if (!ms || ms > 200u)
			ms = 100u;
		end = ms * (SYSTEM_TicksPerSec() / 1000u);
		t0 = SYSTEM_Raw();
		do {
			uint16_t r = BK4819_ReadRegister((uint8_t)reg);
			t = SYSTEM_Raw();
			p[2u * n] = (uint16_t)(ticks_to_us(t - t0) / 4u);
			p[2u * n + 1u] = r;
			n++;
		} while (t - t0 < end && n < max);
		for (unsigned i = 0; i < n; ) {
			o = put(b, "@");
			for (unsigned k = 0; k < 10u && i < n; k++, i++) {
				o = APPS_PutInt(o, p[2u * i] * 4);
				*o++ = ':';
				o = APPS_PutInt(o, p[2u * i + 1u]);
				*o++ = ' ';
			}
			o[-1] = 0;
			MON_Line(b);
		}
		s_n = 0;                          /* the buffer held the trace */
		s_code.ok = false;
		s_redraw = true;
		return true;
	}
	if (mword(&a, "tune")) {             /* tune <10 Hz units> */
		uint32_t f = mdec(&a);
		if (f < 1800000u || f > 66000000u)
			return false;
		tune(f);
		return true;
	}
	if (mword(&a, "margin")) {           /* margin <raw units above the floor, 0.5 dB each> */
		uint32_t m = mdec(&a);
		if (!m || m > 120u)
			return false;
		s_margin = (uint8_t)m;
		return true;
	}
	if (mword(&a, "find")) {
		find();
		return true;
	}
	MON_Line("@app: stat cap [ms] raw sub [raw] trace [ms] [reg] src REG MASK INV tune F10HZ margin N find");
	return mword(&a, "help");
}
#endif

/* ------------------------------------------------------------------ the app */
static void enter(void)
{
	uint32_t len;
	s_d = (int32_t *)(void *)UPDATE_Scratch(&len);
	s_max = len / sizeof(int32_t);
	s_n = 0;
	s_code.ok = false;
	s_msg[0] = 0;
	s_vfo_was = gVfo;
	APP_HoldSaves(true);
	gVfo.tx_freq = 0;
	gVfo.rx_only = true;
	gVfo.rx_tone = gVfo.tx_tone = 0;
	gVfo.narrow = false;
	gVfo.mod = MOD_AM;
	gVfo.ptt_id = 0;
	tune(PRESETS[s_preset]);
#if DEBUGGING
	gMonApp = mon;
#endif
	s_redraw = true;
}

static void leave(void)
{
#if DEBUGGING
	gMonApp = 0;
#endif
	gVfo = s_vfo_was;
	RADIO_Apply();
	APP_HoldSaves(false);
}

static AppsResult_t key(KEY_Code_t k)
{
	if (k == KEY_ACT_EXIT)
		return APPS_EXIT;
	if (k == KEY_1)
		capture(CAPTURE_MS);
	else if (k == KEY_5)
		find();
	else if (k == KEY_0) {
		s_preset = (uint8_t)((s_preset + 1u) % NPRESET);
		tune(PRESETS[s_preset]);
		msg("");
	} else if (k == KEY_ACT_UP)
		tune(gVfo.freq + 2500u);
	else if (k == KEY_ACT_DOWN)
		tune(gVfo.freq - 2500u);
	s_redraw = true;
	return APPS_STAY;
}

static AppsResult_t tick(bool full)
{
	if ((s_ms = (uint16_t)(s_ms + APP_TICK_MS)) >= 250u) {   /* the RSSI line */
		s_ms = 0;
		s_redraw = true;
	}
	if (full || s_redraw) {
		draw(full);
		s_redraw = false;
	}
	return APPS_STAY;
}

const AppDef_t APP_SUBGHZ = { "SUB-GHZ", enter, key, tick, leave, false, false,
	"RECEIVE ONLY\n1     CAPTURE 3 S\n5     FIND: HOLD REMOTE\n0     NEXT PRESET\nUP/DN 25 KHZ\nEXIT  BACK\n\n"
	"CODES: PT2262/EV1527\nPC: TOOLS/SUBGET.PY" };
#endif
