/* Open-UV5R-Mini: scanning timing. See radio/scan.h. */
#include "radio/scan.h"

static bool s_on, s_hold;
static int s_dir;
static ScanResume_t s_mode;
static uint16_t s_tick;
static uint32_t s_ms, s_quiet_ms;

void SCAN_Start(int dir, ScanResume_t mode, uint16_t tick_ms)
{
	s_on = true;
	s_hold = false;
	s_dir = dir < 0 ? -1 : 1;
	s_mode = mode <= SCAN_RESUME_SEARCH ? mode : SCAN_RESUME_CARRIER;
	s_tick = tick_ms ? tick_ms : 10u;
	s_ms = s_quiet_ms = 0;
}

void SCAN_SetTick(uint16_t tick_ms) { s_tick = tick_ms ? tick_ms : 1u; }
void SCAN_Stop(void) { s_on = s_hold = false; }
bool SCAN_Active(void) { return s_on; }
bool SCAN_Holding(void) { return s_on && s_hold; }
int  SCAN_Dir(void) { return s_dir; }

ScanAction_t SCAN_Tick(bool open)
{
	if (!s_on)
		return SCAN_IDLE;
	s_ms += s_tick;
	if (!s_hold) {
		if (open) {
			if (s_mode == SCAN_RESUME_SEARCH) {
				s_on = false;
				return SCAN_IDLE;
			}
			s_hold = true;
			s_ms = s_quiet_ms = 0;
			return SCAN_NONE;
		}
		if (s_ms >= SCAN_DWELL_MS) {
			s_ms = 0;
			return SCAN_HOP;
		}
		return SCAN_NONE;
	}
	/* holding on a signal */
	s_quiet_ms = open ? 0 : s_quiet_ms + s_tick;
	if ((s_mode == SCAN_RESUME_TIME && s_ms >= SCAN_TIME_HOLD_MS)
	    || (s_mode == SCAN_RESUME_CARRIER && s_quiet_ms >= SCAN_CARRIER_MS)
	    || (s_mode == SCAN_RESUME_TIME && s_quiet_ms >= SCAN_CARRIER_MS)) {
		s_hold = false;
		s_ms = 0;
		return SCAN_HOP;
	}
	return SCAN_NONE;
}
