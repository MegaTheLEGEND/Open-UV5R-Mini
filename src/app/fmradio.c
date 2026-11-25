/* Open-UV5R-Mini: FM broadcast radio (ENABLE_APP_FM), 64-108 MHz, on the AC695N's own FM receiver through the
 * speaker (platform/jl_sdk/fmrx.h). Stock has one too (its FM RADIO side-key action); this is ours, on JieLi's FM
 * library from the public SDK.
 *
 * Keys: UP/DOWN 100 kHz; digits type a frequency (4 digits from 100 MHz up, "1015" = 101.5, else 3, "885" = 88.5;
 * MENU takes it early); = searches up to the next station; * and # volume; EXIT goes back. PTT closes it (the BK4819
 * has the speaker again) as for the other apps. The BK4819's receiver is held quiet while FM plays (RADIO_Probe*).
 *
 * It does not start by itself (Sean's first try, 2026-11-24, restarted the radio during the start): the screen first
 * shows how far the last start got (breadcrumbs that survive a restart, fmrx.h), then MENU starts the FM radio, 1 the
 * FM receiver alone (no sound), 2 the DAC alone with a 1 kHz tone.
 *
 * Once running, the three dim lines are (Sean, 2026-11-24): S = how far the start got (9 = all),
 * CK = the clock registers 0x1E0008 / 0x1E000C, H = heap bytes, A = library asserts; IRQ/s should be about 1172 and
 * OUT/s about 48000 when the CPU keeps up. Not tried on a radio yet. */
#include "app/apps.h"
#if APP_FM_ENABLE
#include "app/app.h"
#include "app/keys.h"
#include "platform/jl_sdk/fmrx.h"
#include "radio/radio.h"
#include "ui/screen_apps.h"
#include "driver/system.h"
#include "ui/helper.h"
#include "ui/theme.h"

#define FM_SEEK_STEP 100u                /* kHz */

static UiFm_t   s_f;
static uint32_t s_khz = 100000u;         /* kept while the radio is on */
static uint8_t  s_vol = 10;
static uint32_t s_typed_v;
static uint8_t  s_typed;
static bool     s_seek;
static uint32_t s_seek_from;
static uint16_t s_ms, s_msg_ms;
static uint32_t s_last_irqs, s_last_out;
static bool     s_ok, s_started;
static uint32_t s_mode;

static char *put_hex(char *p, uint32_t v, int digits)
{
	for (int i = digits - 1; i >= 0; i--) {
		uint32_t d = (v >> (4 * i)) & 0xFu;
		*p++ = (char)(d < 10u ? '0' + d : 'A' + d - 10u);
	}
	*p = 0;
	return p;
}

static void copy(char *d, const char *s, unsigned n)
{
	while (n-- > 1 && *s)
		*d++ = *s++;
	*d = 0;
}

static void freq_text(void)
{
	char *p = APPS_PutInt(s_f.freq, (int)(s_khz / 1000u));
	*p++ = '.';
	*p++ = (char)('0' + s_khz / 100u % 10u);
	*p = 0;
}

static void say(const char *t, bool warn)
{
	copy(s_f.bottom, t, sizeof s_f.bottom);
	s_f.bottom_warn = warn;
	s_msg_ms = 2000;
}

static void hint(void)
{
	copy(s_f.bottom, s_seek ? "SEARCHING..." : "=:SEEK */#:VOL", sizeof s_f.bottom);
	s_f.bottom_warn = false;
}

static void typed_text(void)
{
	char *p = s_f.bottom;

	copy(p, "TUNE ", sizeof s_f.bottom);
	while (*p)
		p++;
	p = APPS_PutInt(p, (int)s_typed_v);
	*p++ = '_';
	*p = 0;
	s_f.bottom_warn = true;
	s_msg_ms = 0;
}

static void tune(uint32_t khz)
{
	if (khz < FMRX_MIN_KHZ)
		khz = FMRX_MAX_KHZ;
	else if (khz > FMRX_MAX_KHZ)
		khz = FMRX_MIN_KHZ;
	s_khz = khz;
	freq_text();
	UI_FmDraw(&s_f, false);              /* the new number first: tuning takes a moment */
	(void)FMRX_Tune(khz);
}

/* digits: "1015" -> 101.5 MHz, "885" -> 88.5 MHz */
static void typed_tune(void)
{
	uint32_t v = s_typed_v, khz;

	s_typed = 0;
	if (v >= 640u && v <= 1080u)
		khz = v * 100u;
	else if (v >= 64u && v <= 108u)
		khz = v * 1000u;
	else {
		say("64-108 MHZ ONLY", true);
		return;
	}
	hint();
	tune(khz);
}

static void diag(void)
{
	FmrxStats_t st;
	char *p;
	uint32_t irqs, outs;

	FMRX_Stats(&st);
	irqs = st.irqs - s_last_irqs;
	outs = st.dac_out - s_last_out;
	s_last_irqs = st.irqs;
	s_last_out = st.dac_out;

	p = s_f.diag[0];                     /* "S9 CK0200/01C2 T0/40": ADC time-outs / calls */
	*p++ = 'S';
	*p++ = (char)('0' + st.step % 10u);
	*p++ = ' '; *p++ = 'C'; *p++ = 'K';
	p = put_hex(p, st.sys_div, 4);
	*p++ = '/';
	p = put_hex(p, st.clk_con0, 4);
	*p++ = ' '; *p++ = 'T';
	p = APPS_PutInt(p, (int)(st.adc_timeouts > 99u ? 99u : st.adc_timeouts));
	*p++ = '/';
	APPS_PutInt(p, (int)(st.adc_calls > 999u ? 999u : st.adc_calls));

	p = s_f.diag[1];                     /* "IRQ/S 1172 PK 12034" */
	copy(p, "IRQ/S ", 7);
	p = APPS_PutInt(p + 6, (int)irqs);
	*p++ = ' '; *p++ = 'P'; *p++ = 'K'; *p++ = ' ';
	APPS_PutInt(p, st.peak);

	p = s_f.diag[2];                     /* "OUT/S 48000 DR 0 A0" */
	copy(p, "OUT/S ", 7);
	p = APPS_PutInt(p + 6, (int)outs);
	*p++ = ' '; *p++ = 'D'; *p++ = 'R'; *p++ = ' ';
	p = APPS_PutInt(p, (int)(st.dac_drop > 99999u ? 99999u : st.dac_drop));
	*p++ = ' '; *p++ = 'A';
	APPS_PutInt(p, (int)(st.asserts > 99u ? 99u : st.asserts));
}

/* the breadcrumbs of the last start, before this one: "LAST 57 G4 M3", "IRQS 0 EXC 0", "PC 01E0ABCD 01E0AB00" */
static void last_try(void)
{
	char *p;

	if (gFmCrumb.magic != FMRX_CRUMB_MAGIC) {
		copy(s_f.diag[0], "NO EARLIER TRY", sizeof s_f.diag[0]);
		return;
	}
	p = s_f.diag[0];
	copy(p, gFmCrumb.step ? "LAST " : "LAST OK ", 9);
	while (*p)
		p++;
	p = APPS_PutInt(p, (int)gFmCrumb.step);
	*p++ = ' '; *p++ = 'G';
	p = APPS_PutInt(p, (int)gFmCrumb.glue);
	*p++ = ' '; *p++ = 'M';
	APPS_PutInt(p, (int)gFmCrumb.mode);

	p = s_f.diag[1];
	copy(p, "IRQS ", 6);
	p = APPS_PutInt(p + 5, (int)(gFmCrumb.irqs > 999999u ? 999999u : gFmCrumb.irqs));
	*p++ = ' '; *p++ = 'E'; *p++ = 'X'; *p++ = 'C'; *p++ = ' ';
	APPS_PutInt(p, (int)gFmCrumb.exc);

	p = s_f.diag[2];
	*p++ = 'P'; *p++ = 'C'; *p++ = ' ';
	p = put_hex(p, gFmCrumb.exc_pc, 8);
	*p++ = ' ';
	put_hex(p, gFmCrumb.exc_rets, 8);
}

/* slow-motion start: "STEP 71 G1043" on the bottom line, then a short wait (fmrx.h gFmTrace) */
static void trace(uint32_t step, uint32_t glue)
{
	char b[22], *p = b;
	const char *q = "STEP ";

	while (*q)
		*p++ = *q++;
	p = APPS_PutInt(p, (int)step);
	*p++ = ' '; *p++ = 'G';
	APPS_PutInt(p, (int)glue);
	UI_PrintPadded(b, 2, 116, COLOR_WARN, COLOR_BG, 21);
	SYSTEM_DelayMs(400);
}

static void start(uint32_t mode)
{
	s_mode = mode;
	s_started = true;
	gFmTrace = trace;
	trace(1, 0);
	copy(s_f.bottom, "STARTING...", sizeof s_f.bottom);
	UI_FmDraw(&s_f, false);
	FMRX_SetVolume(s_vol);
	s_ok = FMRX_Open(s_khz, mode);
	gFmTrace = 0;
	diag();
	if (!s_ok)
		say("FM DID NOT START", true);
	else if (!(mode & FMRX_RX))
		copy(s_f.bottom, "1 KHZ TONE */#:VOL", sizeof s_f.bottom);
	else
		hint();
	UI_FmDraw(&s_f, false);
}

static void level(void)
{
	int r = FMRX_Rssi();
	s_f.level = (uint8_t)(r <= 0 ? 0 : r >= 60 ? 100 : r * 100 / 60);
	s_f.stereo = FMRX_Stereo();
}

static void enter(void)
{
	s_typed = 0;
	s_seek = false;
	s_ms = 0;
	s_msg_ms = 0;
	s_last_irqs = s_last_out = 0;
	s_f = (UiFm_t){ 0 };
	s_f.vol = s_vol;
	s_ok = s_started = false;
	freq_text();
	last_try();
	copy(s_f.bottom, "MENU:FM 1:RX 2:TONE", sizeof s_f.bottom);
	s_f.bottom_warn = true;
	UI_FmDraw(&s_f, true);
	RADIO_ProbeBegin();                  /* the BK4819 quiet: no squelch, no audio */
}

static void leave(void)
{
	FMRX_Close();
	RADIO_ProbeEnd();
}

static AppsResult_t key(KEY_Code_t k)
{
	int d = KEY_Digit(k);

	if (!s_started) {                    /* the start screen */
		if (k == KEY_ACT_EXIT)
			return APPS_EXIT;
		if (k == KEY_ACT_MENU)
			start(FMRX_DAC | FMRX_RX);
		else if (k == KEY_1)
			start(FMRX_RX);
		else if (k == KEY_2)
			start(FMRX_DAC);
		return APPS_STAY;
	}
	if (!(s_mode & FMRX_RX) && d >= 0)
		d = -1;                          /* tone test: no tuning */
	if (d >= 0) {
		s_seek = false;
		s_typed_v = s_typed ? s_typed_v * 10u + (uint32_t)d : (uint32_t)d;
		s_typed++;
		/* 4 digits from 100 MHz up ("1015"), 3 below ("885") */
		if (s_typed >= 4u || (s_typed == 3u && s_typed_v / 100u != 1u))
			typed_tune();
		else
			typed_text();
	} else if (s_typed && k == KEY_ACT_MENU)
		typed_tune();
	else if (s_typed && k == KEY_ACT_EXIT) {
		s_typed = 0;
		hint();
	} else if (k == KEY_ACT_EXIT)
		return APPS_EXIT;
	else if (k == KEY_ACT_UP || k == KEY_ACT_DOWN) {
		s_seek = false;
		tune(k == KEY_ACT_UP ? s_khz + FM_SEEK_STEP : s_khz - FM_SEEK_STEP);
		hint();
	} else if (k == KEY_EQUALS) {
		s_seek = !s_seek;
		s_seek_from = s_khz;
		hint();
	} else if (k == KEY_STAR || k == KEY_HASH) {
		if (k == KEY_STAR && s_vol > 0)
			s_vol--;
		else if (k == KEY_HASH && s_vol < 15)
			s_vol++;
		s_f.vol = s_vol;
		FMRX_SetVolume(s_vol);
	}
	UI_FmDraw(&s_f, false);
	return APPS_STAY;
}

static AppsResult_t tick(bool full)
{
	if (!s_started) {
		if (full)
			UI_FmDraw(&s_f, true);
		return APPS_STAY;
	}
	if (s_ok && !(s_mode & FMRX_RX))
		FMRX_ToneFill();
	if (s_ok && s_seek) {                /* one step a tick, stop on a station or after the whole band */
		uint32_t f = s_khz + FM_SEEK_STEP > FMRX_MAX_KHZ ? FMRX_MIN_KHZ : s_khz + FM_SEEK_STEP;
		s_khz = f;
		freq_text();
		if (FMRX_Tune(f) || f == s_seek_from) {
			s_seek = false;
			hint();
		}
		UI_FmDraw(&s_f, false);
	}
	if (s_msg_ms && (s_msg_ms = s_msg_ms > APP_TICK_MS ? (uint16_t)(s_msg_ms - APP_TICK_MS) : 0) == 0)
		hint();
	if ((s_ms = (uint16_t)(s_ms + APP_TICK_MS)) >= 1000u) {
		s_ms = 0;
		diag();
		if (s_ok && !s_seek)
			level();
		UI_FmDraw(&s_f, false);
	}
	if (full)
		UI_FmDraw(&s_f, true);
	return APPS_STAY;
}

const AppDef_t APP_FM = { "FM RADIO", enter, key, tick, leave, false, false };
#endif
