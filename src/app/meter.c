/* Open-UV5R-Mini: signal meter (ENABLE_APP_METER), for fox hunting, after F4HWN's Fox Hunt app.
 *
 * It works on the selected side, as the main screen does (APP_* in app/apps.h), so the receiver, squelch and
 * audio are as usual and PTT (A or B) transmits on what it shows, with the channel's tones and shift; the meter
 * stays open while transmitting (Sean, 2026-11-24). Every METER_SAMPLE_MS the RSSI is read: shown large in dBm,
 * as S-units (IARU Region 1 for VHF: S9 = -93 dBm, 6 dB an S-unit), as a bar with the peak held, and in a history
 * graph scrolling left.
 *
 * Keys: VM switches channel mode (the title shows "MR 005" and the channel's name) and VFO mode (the frequency);
 * UP/DOWN the next channel or a frequency step; digits a channel number (3) or a frequency (6, MHz then kHz), MENU
 * takes fewer, EXIT drops them; * the tick sound (a short beep, faster as the signal gets stronger); # clears the
 * peak; EXIT goes back. The beep takes the receiver off for its 15 ms (radio.c), so the reading skips then. */
#include "app/apps.h"
#if APP_METER_ENABLE
#include "app/app.h"
#include "app/keys.h"
#include "radio/radio.h"
#include "ui/screen_apps.h"
#include "ui/screen_main.h"

#define METER_SAMPLE_MS 100u
#define METER_SAY_MS    2000u

static UiMeter_t s_m;
static uint16_t  s_ms, s_tick_ms;
static uint32_t  s_typed_v;
static uint8_t   s_typed;
static uint16_t  s_say_ms;               /* a message on the bottom line, for this long */

static int16_t dbm_now(void)
{
	uint16_t raw = RADIO_Rssi();
	return raw ? (int16_t)((int)(raw / 2u) - 160) : -160;
}

/* S-units from dBm: S9 = -93, 6 dB each below; "S9+10" above */
static void sunits(char *b, int16_t dbm)
{
	if (dbm >= -93) {
		int over = (dbm + 93) / 10 * 10;
		*b++ = 'S'; *b++ = '9';
		if (over > 0) {
			*b++ = '+';
			b = APPS_PutInt(b, over > 60 ? 60 : over);
		}
		*b = 0;
		return;
	}
	{
		int s = 9 - (-93 - dbm + 5) / 6;
		if (s < 0)
			s = 0;
		*b++ = 'S';
		*b++ = (char)('0' + s);
		*b = 0;
	}
}

static uint8_t hist_h(int16_t dbm)        /* -130..-40 dBm -> 0..50 */
{
	int v = dbm < -130 ? -130 : dbm > -40 ? -40 : dbm;
	return (uint8_t)((v + 130) * 50 / 90);
}

static void copy(char *d, const char *s, unsigned n)
{
	while (n-- > 1 && *s)
		*d++ = *s++;
	*d = 0;
}

/* the title: "MR 005" and the name (or the frequency), or "VFO" and the frequency */
static void title(void)
{
	int ch = APP_Channel();
	const char *n = APP_ChannelName();

	if (ch >= 0) {
		unsigned k = (unsigned)ch + 1u;
		char *p = s_m.left;
		*p++ = 'M'; *p++ = 'R'; *p++ = ' ';
		*p++ = (char)('0' + k / 100u);
		*p++ = (char)('0' + k / 10u % 10u);
		*p++ = (char)('0' + k % 10u);
		*p = 0;
		if (n && n[0] && n[0] != ' ') {
			copy(s_m.right, n, sizeof s_m.right);
			return;
		}
	} else
		copy(s_m.left, "VFO", sizeof s_m.left);
	UI_FormatFreq(s_m.right, gVfo.freq);
}

/* a new channel or frequency: the peak and the history start again */
static void fresh(void)
{
	title();
	s_m.dbm = dbm_now();
	s_m.peak_dbm = s_m.dbm;
	s_m.hist_n = 0;
	sunits(s_m.sunits, s_m.dbm);
}

static void say(const char *t)
{
	copy(s_m.bottom, t, sizeof s_m.bottom);
	s_say_ms = METER_SAY_MS;
}

/* "TUNE CH 01_" or "TUNE 145.5__" while digits are typed */
static void typed_text(void)
{
	char *p = s_m.bottom;
	uint32_t v = s_typed_v;
	bool ch = APP_ChannelMode();
	int n = ch ? 3 : 7;

	copy(p, ch ? "TUNE CH " : "TUNE ", sizeof s_m.bottom);
	while (*p)
		p++;
	for (int k = 0; k < n; k++)
		p[k] = (!ch && k == 3) ? '.' : '_';
	for (int k = s_typed - 1; k >= 0; k--) {
		p[(ch || k < 3) ? k : k + 1] = (char)('0' + v % 10u);
		v /= 10u;
	}
	p[n] = 0;
	s_say_ms = 0;
}

/* the digits typed: a channel (3) or a frequency (6; fewer with MENU, the rest 0) */
static void typed_tune(void)
{
	bool ch = APP_ChannelMode(), ok;

	if (ch)
		ok = APP_TuneChannel((int)s_typed_v - 1);
	else {
		uint32_t khz = s_typed_v;
		for (int k = s_typed; k < 6; k++)
			khz *= 10u;
		ok = APP_TuneFreq(khz * 100u);
	}
	s_typed = 0;
	s_m.bottom[0] = 0;
	if (ok)
		fresh();
	else
		say(ch ? "NO SUCH CHANNEL" : "18-660 MHZ ONLY");
}

static void enter(void)
{
	s_m.tick_sound = false;
	s_m.bottom[0] = 0;
	s_m.tx = false;
	s_typed = 0;
	s_say_ms = 0;
	s_ms = s_tick_ms = 0;
	fresh();
}

static void leave(void) {}

static AppsResult_t key(KEY_Code_t k)
{
	int d = KEY_Digit(k);

	if (d >= 0) {
		s_typed_v = s_typed ? s_typed_v * 10u + (uint32_t)d : (uint32_t)d;
		s_typed++;
		if (s_typed >= (APP_ChannelMode() ? 3u : 6u))
			typed_tune();
		else
			typed_text();
	} else if (s_typed && k == KEY_ACT_MENU)
		typed_tune();
	else if (s_typed && k == KEY_ACT_EXIT) {
		s_typed = 0;
		s_m.bottom[0] = 0;
	} else if (k == KEY_ACT_EXIT)
		return APPS_EXIT;
	else if (k == KEY_VM) {               /* M/FR: channel mode or VFO mode */
		s_typed = 0;
		s_m.bottom[0] = 0;
		APP_SetChannelMode(!APP_ChannelMode());
		fresh();
	} else if (k == KEY_ACT_UP || k == KEY_ACT_DOWN) {
		APP_Step(k == KEY_ACT_UP ? +1 : -1);
		fresh();
	} else if (k == KEY_STAR)
		s_m.tick_sound = !s_m.tick_sound;
	else if (k == KEY_HASH)
		s_m.peak_dbm = s_m.dbm;
	UI_MeterDraw(&s_m, false);
	return APPS_STAY;
}

static AppsResult_t tick(bool full)
{
	const char *why = APP_TxRefused();
	bool tx = RADIO_State() == RADIO_TX;

	if (tx != s_m.tx) {
		s_m.tx = tx;
		full = full || !tx;              /* back from transmit: the screen as it was */
	}
	if (why && !s_typed)                  /* PTT held but refused: say why while it is held */
		say(why);
	if (s_say_ms && (s_say_ms = s_say_ms > APP_TICK_MS ? (uint16_t)(s_say_ms - APP_TICK_MS) : 0) == 0)
		s_m.bottom[0] = 0;
	s_ms = (uint16_t)(s_ms + APP_TICK_MS);
	if (s_ms >= METER_SAMPLE_MS) {
		s_ms = 0;
		if (!tx) {
			s_m.dbm = dbm_now();
			if (s_m.dbm > s_m.peak_dbm)
				s_m.peak_dbm = s_m.dbm;
			sunits(s_m.sunits, s_m.dbm);
			s_m.open = RADIO_State() == RADIO_RX_OPEN;
			if (s_m.hist_n < UI_METER_HIST)
				s_m.hist[s_m.hist_n++] = hist_h(s_m.dbm);
			else {
				for (int i = 1; i < UI_METER_HIST; i++)
					s_m.hist[i - 1] = s_m.hist[i];
				s_m.hist[UI_METER_HIST - 1] = hist_h(s_m.dbm);
			}
		}
		UI_MeterDraw(&s_m, full);
		full = false;
	}
	if (full)
		UI_MeterDraw(&s_m, true);
	/* tick sound: every 1000 ms at -130 dBm down to every 60 ms at -50 */
	if (s_m.tick_sound && !tx) {
		int v = s_m.dbm < -130 ? -130 : s_m.dbm > -50 ? -50 : s_m.dbm;
		uint16_t every = (uint16_t)(1000 - (v + 130) * 940 / 80);
		s_tick_ms = (uint16_t)(s_tick_ms + APP_TICK_MS);
		if (s_tick_ms >= every) {
			s_tick_ms = 0;
			RADIO_Beep(1800, 15);
		}
	}
	return APPS_STAY;
}

const AppDef_t APP_METER = { "SIGNAL METER", enter, key, tick, leave, false, true, "UP/DN   CH OR STEP\n0-9     FREQ OR CH\nVM      VFO / MR\n* F+DN  TICK SOUND\nF+UP    PEAK RESET\nPTT     TRANSMIT\nEXIT    LEAVE" };
#endif
