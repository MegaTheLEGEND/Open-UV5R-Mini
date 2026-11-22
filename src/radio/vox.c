/* Open-UV5R-Mini: VOX. See radio/vox.h. */
#include "radio/vox.h"

static const uint8_t THRESHOLD[10] = { 21, 31, 41, 51, 61, 71, 81, 91, 101, 111 };   /* stock 0x01E65721 */
static uint32_t s_hold_ms;

void VOX_Reset(void) { s_hold_ms = 0; }

VoxAction_t VOX_Tick(uint8_t mic, uint8_t level, uint8_t delay, bool transmitting, uint16_t tick_ms)
{
	uint8_t t = THRESHOLD[level < 10 ? level : 9];

	if (transmitting)
		t = (uint8_t)(t > 8 ? t - 8 : 0);
	if (mic > t) {
		s_hold_ms = (uint32_t)((delay > 15 ? 15 : delay) + 5u) * 100u;
		return transmitting ? VOX_NONE : VOX_START;
	}
	if (!transmitting)
		return VOX_NONE;
	if (s_hold_ms > tick_ms) {
		s_hold_ms -= tick_ms;
		return VOX_NONE;
	}
	s_hold_ms = 0;
	return VOX_STOP;
}
