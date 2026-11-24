/* Open-UV5R-Mini: signal meter (ENABLE_APP_METER), for fox hunting, after F4HWN's Fox Hunt app.
 *
 * The receiver stays as it is (the squelch and audio work as usual); every METER_SAMPLE_MS the RSSI is read: shown
 * large in dBm, as S-units (IARU Region 1 for VHF: S9 = -93 dBm, 6 dB an S-unit), as a bar with the peak held, and
 * in a history graph scrolling left. * turns a tick sound on: a short beep that comes faster as the signal gets
 * stronger (a Geiger counter). 0 clears the peak. EXIT goes back to the menu.
 *
 * Not tried on a radio yet. The beep takes the receiver off for its 15 ms (radio.c), so the reading skips then. */
#include "app/apps.h"
#if APP_METER_ENABLE
#include "app/app.h"
#include "app/keys.h"
#include "radio/radio.h"
#include "ui/screen_apps.h"
#include "ui/screen_main.h"

#define METER_SAMPLE_MS 100u


static UiMeter_t s_m;
static uint16_t  s_ms, s_tick_ms;

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

static void enter(void)
{
	UI_FormatFreq(s_m.freq, gVfo.freq);
	s_m.dbm = dbm_now();
	s_m.peak_dbm = s_m.dbm;
	s_m.hist_n = 0;
	s_m.tick_sound = false;
	s_ms = s_tick_ms = 0;
	sunits(s_m.sunits, s_m.dbm);
}

static void leave(void) {}

static AppsResult_t key(KEY_Code_t k)
{
	if (k == KEY_ACT_EXIT)
		return APPS_EXIT;
	if (k == KEY_STAR)
		s_m.tick_sound = !s_m.tick_sound;
	else if (k == KEY_0)
		s_m.peak_dbm = s_m.dbm;
	UI_MeterDraw(&s_m, false);
	return APPS_STAY;
}

static AppsResult_t tick(bool full)
{
	s_ms = (uint16_t)(s_ms + APP_TICK_MS);
	if (s_ms >= METER_SAMPLE_MS) {
		s_ms = 0;
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
		UI_MeterDraw(&s_m, full);
		full = false;
	}
	if (full)
		UI_MeterDraw(&s_m, true);
	/* tick sound: every 1000 ms at -130 dBm down to every 60 ms at -50 */
	if (s_m.tick_sound) {
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

const AppDef_t APP_METER = { "SIGNAL METER", enter, key, tick, leave };
#endif
