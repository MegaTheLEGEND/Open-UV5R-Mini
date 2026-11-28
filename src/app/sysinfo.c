/* Open-UV5R-Mini: system info (ENABLE_APP_SYSINFO), after F4HWN's System Info app: battery, calibration, external
 * flash, channels in use, transmit lock, uptime, version, build, and whether this run began after a restart.
 * Refreshed every second; any key goes back. Opened from MENU -> ABOUT with 1 (since 1.0.0). */
#include "app/apps.h"
#if APP_SYSINFO_ENABLE
#include "app/app.h"
#include "app/battery.h"
#include "driver/crash.h"
#include "app/version.h"
#include "radio/txlock.h"
#include "store/calib.h"
#include "store/channels.h"
#include "ui/screen_apps.h"

static UiInfo_t s_p;
static char     s_batt[14], s_chans[10], s_up[12], s_lock[14];
static uint16_t s_ms;

static void fill(void)
{
	char *p = s_batt;
	uint16_t mv = BATTERY_Millivolts();
	uint32_t t = gAppsUptimeMs / 1000u;

	if (BATTERY_Raw()) {
		p = APPS_PutInt(p, mv / 1000u);
		*p++ = '.';
		*p++ = (char)('0' + mv / 100u % 10u);
		*p++ = (char)('0' + mv / 10u % 10u);
		*p++ = 'V';
		*p++ = ' ';
		p = APPS_PutInt(p, BATTERY_Percent());
		*p++ = '%';
		*p = 0;
	} else
		s_batt[0] = '-', s_batt[1] = 0;

	p = APPS_PutInt(s_up, (int)(t / 3600u));
	*p++ = ':';
	*p++ = (char)('0' + t / 600u % 6u);
	*p++ = (char)('0' + t / 60u % 10u);
	*p++ = ':';
	*p++ = (char)('0' + t % 60u / 10u);
	*p++ = (char)('0' + t % 10u);
	*p = 0;
}

static void enter(void)
{
	int used = 0;
	char *p;
	const char *q;

	if (gAppsFlashOk)                    /* channels in use: one pass over the 999 slots */
		for (int n = CHANNEL_Next(-1, +1), first = n; n >= 0; ) {
			used++;
			n = CHANNEL_Next(n, +1);
			if (n == first || n < 0)
				break;
		}
	p = APPS_PutInt(s_chans, used);
	q = " USED";
	while (*q)
		*p++ = *q++;
	*p = 0;

	p = s_lock;
	for (q = TXLOCK_ModeName(TXLOCK_Mode(), TXLOCK_Region()); *q && p < s_lock + 8; )
		*p++ = *q++;
	*p++ = ' ';
	for (q = TXLOCK_RegionName(TXLOCK_Region()); *q && p < s_lock + 13; )
		*p++ = *q++;
	*p = 0;

	s_p.title = "SYSTEM INFO";
	s_p.label[0] = "BATTERY";  s_p.value[0] = s_batt;
	s_p.label[1] = "CALIB";    s_p.value[1] = CALIB_Present() ? "OK" : "MISSING";
	s_p.label[2] = "FLASH";    s_p.value[2] = gAppsFlashOk ? "OK" : "NOT FOUND";
	s_p.label[3] = "CHANNELS"; s_p.value[3] = s_chans;
	s_p.label[4] = "TX LOCK";  s_p.value[4] = s_lock;
	s_p.label[5] = "UPTIME";   s_p.value[5] = s_up;
	s_p.label[6] = "VERSION";  s_p.value[6] = FW_VERSION;
	s_p.label[7] = "BUILD";    s_p.value[7] = VERSION_Build();
	/* the radio kind is on the ABOUT screen this opens from; here how this run started (driver/crash.h) */
	s_p.label[8] = "START";    s_p.value[8] = CRASH_Safe() ? "SAFE START" : CRASH_Restarted() ? "AFTER RESTART" : "NORMAL";
	s_p.hint = "ANY KEY: BACK";
	s_ms = 0;
	fill();
}

static void leave(void) {}

static AppsResult_t key(KEY_Code_t k)
{
	(void)k;
	return APPS_EXIT;
}

static AppsResult_t tick(bool full)
{
	if ((s_ms = (uint16_t)(s_ms + APP_TICK_MS)) >= 1000u) {   /* the tick is not always 10 ms */
		s_ms = 0;
		fill();
		UI_InfoDraw(&s_p, full);
	} else if (full)
		UI_InfoDraw(&s_p, true);
	return APPS_STAY;
}

const AppDef_t APP_SYSINFO = { "SYSTEM INFO", enter, key, tick, leave, false, false, "ANY KEY LEAVE" };
#endif
