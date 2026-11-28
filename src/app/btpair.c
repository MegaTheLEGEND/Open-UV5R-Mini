/* Open-UV5R-Mini: pairing mode in the Bluetooth link apps (app/btpair.h). */
#include "app/btpair.h"
#include "app/apps.h"
#include "bt/bt.h"

static uint32_t s_until;                 /* gAppsUptimeMs when pairing mode ends */
static char     s_last[22];

void BTPAIR_Enter(void)
{
	BT_SetPairable(false);
	s_until = 0;
	s_last[0] = 0;
}

void BTPAIR_Leave(void)
{
	BT_SetPairable(false);
	s_until = 0;
}

bool BTPAIR_Key(KEY_Code_t k)
{
	if (k != KEY_5)
		return false;
	if (BT_Pairable()) {
		BT_SetPairable(false);
		s_until = 0;
	} else {
		BT_SetPairable(true);
		s_until = gAppsUptimeMs + PAIR_SECONDS * 1000u;
	}
	return true;
}

static char *put(char *o, const char *s) { while (*s) *o++ = *s++; *o = 0; return o; }

bool BTPAIR_Tick(char *line, bool *big)
{
	int32_t pk = BT_Passkey();
	BT_PairState_t st = BT_PairState();
	char *o = line;
	bool changed;

	*big = false;
	if (BT_Pairable() && (int32_t)(gAppsUptimeMs - s_until) >= 0) {
		BT_SetPairable(false);           /* time is up */
		s_until = 0;
	}
	if (st == BT_PAIR_PAIRED && BT_Pairable()) {
		BT_SetPairable(false);           /* done: no second phone by accident */
		s_until = 0;
	}
	if (pk >= 0) {                       /* type these on the phone or PC */
		o = put(o, "KEY ");
		for (int32_t d = 100000; d; d /= 10)
			*o++ = (char)('0' + pk / d % 10);
		*o = 0;
		*big = true;
	} else if (st == BT_PAIR_PAIRED)
		put(o, "PAIRED, ENCRYPTED");
	else if (st == BT_PAIR_FAILED)
		put(o, "PAIR FAILED  5 AGAIN");
	else if (BT_Pairable()) {
		uint32_t left = (s_until - gAppsUptimeMs + 999u) / 1000u;
		o = put(o, "PAIRING ON ");
		o = APPS_PutInt(o, (int)left);
		put(o, "S  5 OFF");
	} else
		put(o, "5 PAIRING MODE");
	changed = false;
	for (int i = 0; i < 22; i++) {
		if (s_last[i] != line[i])
			changed = true;
		s_last[i] = line[i];
		if (!line[i])
			break;
	}
	return changed;
}
