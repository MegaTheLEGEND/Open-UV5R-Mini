/* Open-UV5R-Mini: a DTMF code sequence (PTT-ID), timed the stock way.
 *
 * Stock (sub_01E4DABA sets it up, 0x01E531D8 sends it): a wait first, then each code for the "on" time followed by
 * silence for the "off" time, up to 16 codes or a 0xFF. The codes are 0-15 (0-9, A, B, C, D, *, #).
 * Times (from the ANI block at external 0xA000, store/settings.h):
 *   - wait: (setting 0x0C x 50 + 50) counts, on and off: table {5, 10, 20, 30, 40, 50}[n] x 5 counts.
 *   - The count is taken here to be 2 ms (the stock timer's 10 ms part is every 5th call, 0x01E55006), so the wait
 *     is 100 ms + 100 ms x setting 0x0C and on / off are 50-500 ms. Inferred, not timed on a radio.
 */
#ifndef APP_DTMFSEQ_H
#define APP_DTMFSEQ_H

#include <stdbool.h>
#include <stdint.h>

enum {
	DTMFSEQ_NONE = -1,   /* nothing to change */
	DTMFSEQ_GAP  = -2,   /* silence between codes */
	DTMFSEQ_DONE = -3,   /* the last code ended: tones off */
};

uint16_t DTMFSEQ_TimeMs(uint8_t n);            /* the on / off times by the ANI block's value (0-5) */
uint16_t DTMFSEQ_WaitMs(uint8_t setting_0c);
/* false (nothing to send) if the first code is 0xFF */
bool     DTMFSEQ_Start(const uint8_t codes[16], uint16_t wait_ms, uint16_t on_ms, uint16_t off_ms);
void     DTMFSEQ_Stop(void);
bool     DTMFSEQ_Busy(void);
/* Every tick: a code (0-15) to start now, or DTMFSEQ_NONE / GAP / DONE. */
int      DTMFSEQ_Tick(uint16_t tick_ms);

#endif
