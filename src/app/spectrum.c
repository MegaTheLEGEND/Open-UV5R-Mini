/* Open-UV5R-Mini: spectrum app (ENABLE_APP_SPECTRUM), after the UV-K5 spectrum (fagci's, in F4HWN's firmware).
 *
 * UI_SPECTRUM_N frequencies one step apart, centred on the VFO frequency: the receiver is moved to each in turn
 * (RADIO_ProbeRssi: tune, VCO re-calibration, SPEC_SETTLE_US, RSSI) and the level drawn as a bar. A few are measured
 * every tick, so a sweep of 64 takes about 0.15 s and keys still answer. The audio is off while it runs.
 *
 * Keys: digits tune the centre as on the main screen (6 digits, MHz then kHz; MENU takes fewer, the rest 0; EXIT
 * drops them); UP/DOWN move the centre a quarter of the span; * and # make the step smaller / larger; = freezes;
 * MENU tunes the VFO to the marked peak and goes back to the main screen; EXIT goes back. PTT (A or B) closes it and
 * transmits on the centre frequency on the selected side (app.c; Sean, 2026-11-24).
 *
 * The wait at each step is F4HWN's 3.2 ms: a first version with 1.5 ms read about 0 everywhere on a radio. */
#include "app/apps.h"
#if APP_SPECTRUM_ENABLE
#include "app/keys.h"
#include "radio/radio.h"
#include "ui/screen_apps.h"

#define SPEC_SETTLE_US   3200u           /* F4HWN's default scan delay; bars on a radio with it (2026-11-24) */
#define SPEC_PER_TICK    3u
#define SPEC_DBM_MIN   (-130)
#define SPEC_DBM_MAX    (-50)

static uint32_t s_centre;
static uint8_t  s_step;                  /* index into RADIO_Steps */
static uint16_t s_rssi[UI_SPECTRUM_N];   /* raw: dBm = raw / 2 - 160 */
static uint8_t  s_pos;                   /* the next column to measure */
static uint8_t  s_peak;                  /* the strongest column of the last full sweep */
static bool     s_frozen, s_swept;
static bool     s_has_peak;              /* the strongest column stands out of the noise */
static uint32_t s_khz;                   /* digits typed for a new centre */
static uint8_t  s_typed;

static uint32_t freq_at(unsigned i)
{
	int32_t off = ((int32_t)i - UI_SPECTRUM_N / 2) * (int32_t)RADIO_Steps[s_step];
	int64_t f = (int64_t)s_centre + off;
	return f < (int64_t)FREQ_MIN ? FREQ_MIN : f > (int64_t)FREQ_MAX ? FREQ_MAX : (uint32_t)f;
}

static int dbm_of(uint16_t raw) { return (int)(raw / 2u) - 160; }

static uint8_t height_of(uint16_t raw)
{
	int d = dbm_of(raw);
	if (d <= SPEC_DBM_MIN)
		return 0;
	if (d >= SPEC_DBM_MAX)
		return UI_SPECTRUM_H;
	return (uint8_t)((d - SPEC_DBM_MIN) * UI_SPECTRUM_H / (SPEC_DBM_MAX - SPEC_DBM_MIN));
}

static void step_text(char *b, uint16_t s)        /* "12.5K", "6.25K", "100K" */
{
	uint32_t whole = s / 100u, frac = s % 100u;
	b = APPS_PutInt(b, (int)whole);
	if (frac) {
		*b++ = '.';
		*b++ = (char)('0' + frac / 10u);
		if (frac % 10u)
			*b++ = (char)('0' + frac % 10u);
	}
	*b++ = 'K';
	*b = 0;
}

static void draw_frame(void)
{
	char st[16], l[10], c[10], r[10];
	uint32_t half = (uint32_t)RADIO_Steps[s_step] * (UI_SPECTRUM_N / 2);   /* 10 Hz */

	step_text(st, RADIO_Steps[s_step]);
	APPS_FormatMhz(c, s_centre);
	/* the edges as offsets from the centre: "-400K" / "+400K", or "-1.6M" above 999 kHz */
	l[0] = '-';
	r[0] = '+';
	if (half >= 100000u) {
		char *p = APPS_PutInt(l + 1, (int)(half / 100000u));
		*p++ = '.';
		*p++ = (char)('0' + half / 10000u % 10u);
		*p++ = 'M';
		*p = 0;
	} else {
		char *p = APPS_PutInt(l + 1, (int)(half / 100u));
		*p++ = 'K';
		*p = 0;
	}
	for (int k = 1; k < 10; k++)
		r[k] = l[k];
	UI_SpectrumFrame(st, l, c, r);
	for (unsigned i = 0; i < UI_SPECTRUM_N; i++)
		UI_SpectrumBar((uint8_t)i, height_of(s_rssi[i]), s_swept && i == s_peak);
}

static void draw_peak(void)
{
	char t[22], *p = t;

	if (s_typed) {                       /* "TUNE 145.5__" while digits are typed */
		uint32_t v = s_khz;
		const char *q = "TUNE ";
		while (*q)
			*p++ = *q++;
		for (int k = 0; k < 7; k++)
			p[k] = k == 3 ? '.' : '_';
		for (int k = s_typed - 1; k >= 0; k--) {
			p[k < 3 ? k : k + 1] = (char)('0' + v % 10u);
			v /= 10u;
		}
		p[7] = 0;
		UI_SpectrumPeak(t, true);
		return;
	}
	if (s_frozen) {
		UI_SpectrumPeak("FROZEN  =: RUN", true);
		return;
	}
	if (!s_swept) {
		UI_SpectrumPeak("", false);
		return;
	}
	if (!s_has_peak) {
		UI_SpectrumPeak("NO SIGNAL", false);
		return;
	}
	*p++ = 'P'; *p++ = 'K'; *p++ = ' ';
	p = APPS_FormatMhz(p, freq_at(s_peak));
	*p++ = ' ';
	p = APPS_PutInt(p, dbm_of(s_rssi[s_peak]));
	*p++ = 'd'; *p++ = 'B'; *p++ = 'm';
	*p = 0;
	UI_SpectrumPeak(t, false);
}

static void restart(void)
{
	for (unsigned i = 0; i < UI_SPECTRUM_N; i++)
		s_rssi[i] = 0;
	s_pos = 0;
	s_swept = false;
	s_has_peak = false;
	gAppsTune = s_centre;                /* PTT transmits here (app.c) */
}

static void enter(void)
{
	s_typed = 0;
	s_centre = gVfo.freq;
	s_step = gVfo.step < RADIO_StepCount ? gVfo.step : 4;
	s_frozen = false;
	restart();
	RADIO_ProbeBegin();
}

static void leave(void)
{
	RADIO_ProbeEnd();
}

/* the typed digits as the new centre (missing ones 0), if it is a frequency the radio has */
static void typed_tune(void)
{
	uint32_t khz = s_khz;

	for (int k = s_typed; k < 6; k++)
		khz *= 10u;
	s_typed = 0;
	if (khz * 100u >= FREQ_MIN && khz * 100u <= FREQ_MAX)
		s_centre = khz * 100u;
}

static AppsResult_t key(KEY_Code_t k)
{
	uint32_t span = (uint32_t)RADIO_Steps[s_step] * UI_SPECTRUM_N;
	int d = KEY_Digit(k);

	if (d >= 0) {                        /* tuning by number, as on the main screen */
		s_khz = s_typed ? s_khz * 10u + (uint32_t)d : (uint32_t)d;
		if (++s_typed < 6) {
			draw_peak();
			return APPS_STAY;
		}
		typed_tune();
	} else if (s_typed && (k == KEY_ACT_EXIT || k == KEY_ACT_MENU)) {
		if (k == KEY_ACT_MENU)
			typed_tune();
		s_typed = 0;
		if (k == KEY_ACT_EXIT) {
			draw_peak();
			return APPS_STAY;
		}
	} else if (k == KEY_ACT_EXIT)
		return APPS_EXIT;
	else if (k == KEY_ACT_MENU) {
		if (!s_has_peak)
			return APPS_STAY;
		gAppsTune = freq_at(s_peak);
		return APPS_TUNE;
	}
	if (k == KEY_ACT_UP && s_centre + span / 4 <= FREQ_MAX)
		s_centre += span / 4;
	else if (k == KEY_ACT_DOWN && s_centre >= FREQ_MIN + span / 4)
		s_centre -= span / 4;
	else if (k == KEY_STAR && s_step > 0)
		s_step--;
	else if (k == KEY_HASH && s_step + 1u < RADIO_StepCount)
		s_step++;
	else if (k == KEY_EQUALS) {
		s_frozen = !s_frozen;
		draw_peak();
		return APPS_STAY;
	} else
		return APPS_STAY;
	restart();
	draw_frame();
	draw_peak();
	return APPS_STAY;
}

static AppsResult_t tick(bool full)
{
	if (full) {
		draw_frame();
		draw_peak();
	}
	if (s_frozen)
		return APPS_STAY;
	for (unsigned n = 0; n < SPEC_PER_TICK; n++) {
		uint8_t i = s_pos;
		bool was_peak = s_has_peak && i == s_peak;
		uint16_t r = RADIO_ProbeRssi(freq_at(i), SPEC_SETTLE_US);
		/* as F4HWN: a rise shows at once, a fall halves the gap each sweep, so the picture holds still */
		s_rssi[i] = r >= s_rssi[i] || !s_swept ? r : (uint16_t)((s_rssi[i] + r) / 2u);
		UI_SpectrumBar(i, height_of(s_rssi[i]), was_peak);
		if (++s_pos >= UI_SPECTRUM_N) {
			/* a sweep done. The peak is marked only when it stands 10 dB over the average (noise alone made the
			 * mark jump from sweep to sweep on a radio, 2026-11-24), and it moves only to a column 3 dB stronger */
			uint8_t old = s_peak, best = 0;
			bool had = s_has_peak;
			uint32_t sum = 0;
			s_pos = 0;
			for (unsigned j = 0; j < UI_SPECTRUM_N; j++) {
				sum += s_rssi[j];
				if (s_rssi[j] > s_rssi[best])
					best = (uint8_t)j;
			}
			if (!had || s_rssi[best] >= s_rssi[old] + 6u)     /* raw RSSI is in 0.5 dB */
				s_peak = best;
			s_has_peak = s_rssi[s_peak] >= sum / UI_SPECTRUM_N + 20u;
			s_swept = true;
			if (had && old != s_peak)
				UI_SpectrumBar(old, height_of(s_rssi[old]), false);
			UI_SpectrumBar(s_peak, height_of(s_rssi[s_peak]), s_has_peak);
			draw_peak();
		}
	}
	return APPS_STAY;
}

const AppDef_t APP_SPECTRUM = { "SPECTRUM", enter, key, tick, leave, true };
#endif
