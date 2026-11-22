/* Open-UV5R-Mini: a DTMF code sequence. See radio/dtmfseq.h. */
#include "radio/dtmfseq.h"

static const uint8_t TIME[6] = { 5, 10, 20, 30, 40, 50 };    /* stock 0x01E6556E */

static uint8_t  s_codes[16], s_idx;
static bool     s_busy, s_tone;
static uint16_t s_left_ms, s_on_ms, s_off_ms;

uint16_t DTMFSEQ_TimeMs(uint8_t n)
{
	return (uint16_t)(TIME[n < 6 ? n : 1] * 5u * 2u);          /* stock: out of range counts as 1 */
}

uint16_t DTMFSEQ_WaitMs(uint8_t setting_0c)
{
	return (uint16_t)((setting_0c * 50u + 50u) * 2u);
}

bool DTMFSEQ_Start(const uint8_t codes[16], uint16_t wait_ms, uint16_t on_ms, uint16_t off_ms)
{
	s_busy = false;
	if (codes[0] > 15)
		return false;
	for (unsigned i = 0; i < 16; i++)
		s_codes[i] = codes[i];
	s_idx = 0;
	s_tone = false;
	s_left_ms = wait_ms;
	s_on_ms = on_ms;
	s_off_ms = off_ms;
	s_busy = true;
	return true;
}

void DTMFSEQ_Stop(void) { s_busy = false; }
bool DTMFSEQ_Busy(void) { return s_busy; }

int DTMFSEQ_Tick(uint16_t tick_ms)
{
	if (!s_busy)
		return DTMFSEQ_NONE;
	if (s_left_ms > tick_ms) {
		s_left_ms -= tick_ms;
		return DTMFSEQ_NONE;
	}
	s_left_ms = 0;
	if (!s_tone) {                                  /* a code now */
		s_tone = true;
		s_left_ms = s_on_ms;
		return s_codes[s_idx];
	}
	s_tone = false;                                 /* its time is up: silence, then the next code */
	if (++s_idx >= 16 || s_codes[s_idx] > 15) {
		s_busy = false;
		return DTMFSEQ_DONE;
	}
	s_left_ms = s_off_ms;
	return DTMFSEQ_GAP;
}
