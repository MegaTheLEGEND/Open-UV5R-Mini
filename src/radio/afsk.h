/* Open-UV5R-Mini: a software Bell 202 AFSK demodulator (1200 bits/s, 1200 / 2200 Hz), for APRS.
 *
 * For a receive audio path into an MCU ADC: none is known on this board yet (ROADMAP 3.12: the BK4819's audio goes
 * to the speaker without the MCU), so on the radio APRS is received through the BK4819's own FSK modem instead
 * (radio/fskrx.c). This one is here for the day an ADC path is found (or wired), and it is what tests the AX.25
 * receiver on the PC (tests/aprs_host.py).
 *
 * How: two quadrature correlators, each over one bit's worth of samples, one at 1200 Hz and one at 2200 Hz; the
 * stronger one gives the line level. A bit clock (PLL) pulled a quarter of the way to each level change samples the
 * level in the middle of each bit and hands it to the AX.25 receiver. Integer only; the sine table is made at start.
 */
#ifndef RADIO_AFSK_H
#define RADIO_AFSK_H

#include <stdint.h>
#include "radio/ax25.h"

#define AFSK_MAX_N 40u                   /* samples a bit: up to 48 kHz */

typedef struct {
	uint32_t inc[2], ph[2];              /* the two reference oscillators (mark, space): phase steps and phases */
	int32_t  ring[4][AFSK_MAX_N];        /* the products in the window: mark I, Q, space I, Q */
	int32_t  sum[4];
	uint8_t  n, pos, shift;
	int32_t  dc;                         /* the input's average, << 8 */
	uint32_t pll, pll_inc;
	bool     level;
	Ax25Rx_t ax;
} Afsk_t;

void     AFSK_Init(Afsk_t *d, uint32_t fs);
/* One sample. Returns the length of a frame just decoded (in d->ax.buf, FCS left off), else 0. */
unsigned AFSK_Sample(Afsk_t *d, int16_t x);

#endif
