/* Open-UV5R-Mini: broadcast FM in the VFO (app/bcast.h). Our code, on the FM receiver driver (platform/jl_sdk). */
#include "app/bcast.h"
#include "app/apps.h"
#if APP_FM_ENABLE
#include "driver/system.h"
#include "platform/jl_sdk/fmrx.h"
#include "radio/radio.h"
#include "radio/rds.h"

static bool s_on;

/* RDS between ticks, as the FM app does: the main loop's wait calls this over and over (gSystemIdle) */
static void idle_rds(void)
{
	if (s_on)
		(void)RDS_Poll();
}

bool BCAST_Start(uint32_t freq, uint8_t volume)
{
	if (s_on)
		return true;
	RADIO_Park(true);                    /* the BK4819 asleep, its audio off */
	FMRX_SetVolume(volume);
	if (!FMRX_Open(freq / 100u, FMRX_DAC | FMRX_RX | FMRX_FAST)) {
		FMRX_Close();
		SYSTEM_SetTicksPerSec(0);
		FMRX_BeepPrepare();
		RADIO_Park(false);
		return false;
	}
	s_on = true;
	RDS_Reset(0);                        /* no station name from an earlier station */
	gSystemIdle = idle_rds;
	return true;
}

void BCAST_Tune(uint32_t freq)
{
	if (s_on)
		(void)FMRX_Tune(freq / 100u);
}

void BCAST_Volume(uint8_t volume)
{
	if (s_on)
		FMRX_SetVolume(volume);
}

void BCAST_Stop(void)
{
	if (!s_on)
		return;
	s_on = false;
	gSystemIdle = 0;
	FMRX_Close();
	SYSTEM_SetTicksPerSec(0);            /* the crystal's rate again (the FM clock is put back) */
	FMRX_BeepPrepare();                  /* the DAC back to the key beeps */
	RADIO_Park(false);                   /* the BK4819 awake on gVfo */
}

bool BCAST_On(void) { return s_on; }

void BCAST_Tick(void)
{
	if (!s_on)
		return;
	BCAST_ClockCheck();
	(void)RDS_Poll();
}

int8_t BCAST_Bars(uint8_t max)
{
	int r = s_on ? FMRX_Rssi() : 0;      /* the library's level, about 0-60 (the FM app's scale) */

	return (int8_t)(r <= 0 ? 0 : r >= 60 ? max : r * max / 60);
}

const char *BCAST_Name(void)
{
	const RdsInfo_t *r = RDS_Info();

	if (!s_on || !r->pi)
		return 0;
	for (int i = 0; i < 8; i++)
		if (r->ps[i] != ' ' && r->ps[i])
			return r->ps;
	return 0;
}

/* The timer's real rate while FM runs, from the FM interrupt: 37500 / 32 = 1171.875 a second, crystal-timed (8192
 * of them in 6.94 s on Sean's radio, PC-timed, 2026-11-26). Measured over about 2 s, so uptime and key holds keep
 * real time whatever the FM start did to the clocks. */
void BCAST_ClockCheck(void)
{
	static uint32_t irq0, raw0;
	static bool armed;
	uint32_t irqs = gFmCrumb.irqs, raw = SYSTEM_Raw(), di = irqs - irq0;

	if (!armed || irqs < irq0) {
		irq0 = irqs;
		raw0 = raw;
		armed = true;
		return;
	}
	if (di < 2344u)
		return;
	{
		uint32_t tps = (raw - raw0) / di * 1172u + (raw - raw0) % di * 1172u / di;
		if (tps > 10000u && tps < 2000000u)  /* something sensible */
			SYSTEM_SetTicksPerSec(tps);
	}
	irq0 = irqs;
	raw0 = raw;
}
#endif
