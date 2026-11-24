/* Open-UV5R-Mini: what the apps share. See app/apps.h. */
#include "app/apps.h"

bool     gAppsFlashOk;
uint32_t gAppsUptimeMs;
uint32_t gAppsTune;
int      gAppsChannel = -1;

char *APPS_PutInt(char *b, int v)
{
	char t[11];
	int n = 0;
	unsigned u;

	if (v < 0) {
		*b++ = '-';
		u = (unsigned)(-v);
	} else
		u = (unsigned)v;
	do
		t[n++] = (char)('0' + u % 10u);
	while ((u /= 10u) && n < 10);
	while (n)
		*b++ = t[--n];
	*b = 0;
	return b;
}

char *APPS_FormatMhz(char *b, uint32_t f)
{
	uint32_t khz = (f + 50u) / 100u;      /* 10 Hz -> kHz, rounded */

	b = APPS_PutInt(b, (int)(khz / 1000u));
	*b++ = '.';
	*b++ = (char)('0' + khz / 100u % 10u);
	*b++ = (char)('0' + khz / 10u % 10u);
	*b++ = (char)('0' + khz % 10u);
	*b = 0;
	return b;
}
