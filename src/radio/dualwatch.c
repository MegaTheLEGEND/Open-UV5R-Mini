/* Open-UV5R-Mini: dual watch timing. See radio/dualwatch.h. */
#include "radio/dualwatch.h"

static bool s_peek, s_held;
static uint32_t s_ms;

void DW_Reset(void) { s_peek = s_held = false; s_ms = 0; }
bool DW_Peeking(void) { return s_peek; }
bool DW_Held(void) { return s_peek && s_held; }

DwAction_t DW_Tick(bool enabled, bool busy, bool open, uint16_t tick_ms)
{
	if (!enabled || busy) {
		bool was = s_peek;
		DW_Reset();
		return was ? DW_END : DW_NONE;
	}
	s_ms += tick_ms;
	if (!s_peek) {
		if (open)
			s_ms = 0;
		else if (s_ms >= DW_ACTIVE_MS) {
			s_peek = true;
			s_held = false;
			s_ms = 0;
			return DW_BEGIN;
		}
		return DW_NONE;
	}
	if (open) {
		bool first = !s_held;
		s_held = true;
		s_ms = 0;
		return first ? DW_HELD : DW_NONE;
	}
	if (s_ms >= (s_held ? DW_HANG_MS : DW_PEEK_MS)) {
		DW_Reset();
		return DW_END;
	}
	return DW_NONE;
}
