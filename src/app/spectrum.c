/* Open-UV5R-Mini: spectrum app (ENABLE_APP_SPECTRUM), after the UV-K5 spectrum (fagci's, in F4HWN's firmware).
 *
 * UI_SPECTRUM_N frequencies one step apart, centred on the VFO frequency: the receiver is moved to each in turn
 * (RADIO_ProbeRssi: tune, VCO re-calibration, SPEC_SETTLE_US, RSSI) and the level drawn as a bar. A few are measured
 * every tick, so a sweep of 64 takes about 0.15 s and keys still answer. The audio is off while it runs.
 *
 * Keys: UP/DOWN move the centre a quarter of the span; * and # make the step smaller / larger; 1 and 3 the wait
 * at each step; 5 freezes; MENU tunes
 * the VFO to the strongest frequency and goes back to the main screen; EXIT goes back to the menu. PTT closes it
 * (app.c).
 *
 * 1 and 3 make the wait at each step shorter / longer (0.4 to 10 ms, shown in the title bar with the step); the
 * first version waited 1.5 ms and read about 0 everywhere on a radio (2026-11-24); now 3.2 ms as F4HWN. */
#include "app/apps.h"
#if APP_SPECTRUM_ENABLE
#include "app/keys.h"
#include "radio/radio.h"
#include "ui/screen_apps.h"

#define SPEC_SETTLE_US   3200u           /* F4HWN's default scan delay; 1 and 3 change it on the radio */
#define SPEC_PER_TICK    3u
#define SPEC_DBM_MIN   (-130)
#define SPEC_DBM_MAX    (-50)

static uint32_t s_centre;
static uint8_t  s_step;                  /* index into RADIO_Steps */
static uint16_t s_rssi[UI_SPECTRUM_N];   /* raw: dBm = raw / 2 - 160 */
static uint8_t  s_pos;                   /* the next column to measure */
static uint8_t  s_peak;                  /* the strongest column of the last full sweep */
static bool     s_frozen, s_swept;
static uint16_t s_settle = SPEC_SETTLE_US;

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
	{                                    /* "12.5K 3.2MS": the step and the wait at each step */
		char *p = st;
		while (*p)
			p++;
		*p++ = ' ';
		p = APPS_PutInt(p, s_settle / 1000u);
		*p++ = '.';
		*p++ = (char)('0' + s_settle / 100u % 10u);
		*p++ = 'M';
		*p++ = 'S';
		*p = 0;
	}
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

	if (s_frozen) {
		UI_SpectrumPeak("FROZEN  5: RUN", true);
		return;
	}
	if (!s_swept) {
		UI_SpectrumPeak("", false);
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
}

static void enter(void)
{
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

static AppsResult_t key(KEY_Code_t k)
{
	uint32_t span = (uint32_t)RADIO_Steps[s_step] * UI_SPECTRUM_N;

	if (k == KEY_ACT_EXIT)
		return APPS_EXIT;
	if (k == KEY_ACT_MENU) {
		if (!s_swept)
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
	else if (k == KEY_1 && s_settle > 400u)
		s_settle = (uint16_t)(s_settle - (s_settle > 2000u ? 800u : 400u));
	else if (k == KEY_3 && s_settle < 10000u)
		s_settle = (uint16_t)(s_settle + (s_settle >= 2000u ? 800u : 400u));
	else if (k == KEY_5) {
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
		bool was_peak = s_swept && i == s_peak;
		s_rssi[i] = RADIO_ProbeRssi(freq_at(i), s_settle);
		UI_SpectrumBar(i, height_of(s_rssi[i]), was_peak);
		if (++s_pos >= UI_SPECTRUM_N) {   /* a sweep done: mark its peak */
			uint8_t old = s_peak;
			s_pos = 0;
			s_peak = 0;
			for (unsigned j = 1; j < UI_SPECTRUM_N; j++)
				if (s_rssi[j] > s_rssi[s_peak])
					s_peak = (uint8_t)j;
			s_swept = true;
			UI_SpectrumBar(old, height_of(s_rssi[old]), false);
			UI_SpectrumBar(s_peak, height_of(s_rssi[s_peak]), true);
			draw_peak();
		}
	}
	return APPS_STAY;
}

const AppDef_t APP_SPECTRUM = { "SPECTRUM", enter, key, tick, leave };
#endif
