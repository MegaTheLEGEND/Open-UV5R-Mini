/* Open-UV5R-Mini: on-off keyed (OOK/ASK) remote controls, 315 / 433.92 MHz (Sean asked, 2026-11-27): receive only.
 *
 * A capture is a list of durations in microseconds, carrier on positive, off negative, the way Flipper Zero's RAW
 * .sub files hold them ("RAW_Data: 350 -1050 ..."). radio/ook.c finds the commonest cheap-remote code in it: the
 * PT2262 / EV1527 family (Flipper calls it "Princeton"): 24 bits, each a pulse and a gap, 0 = short pulse then a gap
 * three times longer, 1 = the other way round, the frames separated by a gap of 31 short units. The short unit
 * ("TE") is often 300-500 us. Written from that public description; tested on the PC (tests/ook_host.py).
 */
#ifndef RADIO_OOK_H
#define RADIO_OOK_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	bool     ok;
	uint8_t  bits;                       /* 24 */
	uint32_t key;                        /* the first bit received is the highest */
	uint16_t te;                         /* the short unit, us */
	uint8_t  frames;                     /* frames with this key (a press repeats it) */
	uint8_t  other;                      /* good frames with another key */
} OokCode_t;

/* The key seen most often in d[0..n); out->ok if at least one whole frame was found */
void OOK_Princeton(const int32_t *d, unsigned n, OokCode_t *out);

#endif
