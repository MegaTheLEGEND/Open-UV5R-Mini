/* Open-UV5R-Mini: APRS receive through the BK4819's own FSK modem (experimental, Sean asked 2026-11-27).
 *
 * The receive audio doesn't reach the MCU (radio/afsk.h), so the chip has to find the bits. Its modem is made
 * for its own packets (preamble, sync word, fixed length), not for AX.25, so it is used this way:
 *   - FFSK 1200 bits/s (1200/1800 or 1200/2400 Hz: Bell 202's 2200 Hz is between, so both are tried);
 *   - the sync word is the AX.25 flags as they sound: NRZI turns the flag 0x7E into seven bits of one tone and one
 *     of the other, 0xFEFEFEFE (or 0x01010101, depending on the tone the line was on: the two take turns);
 *   - a packet of FSKRX_LEN bytes after it is read from the FIFO and fed, as line levels, to the AX.25 receiver.
 *     Which byte of a FIFO word comes first, and which bit, isn't known, so four receivers each take one order.
 * Nothing of this is tried on a radio yet; the counts on the APRS screen say how far it gets.
 */
#ifndef RADIO_FSKRX_H
#define RADIO_FSKRX_H

#include <stdbool.h>
#include <stdint.h>

#define FSKRX_LEN 250u                   /* bytes after the sync word: about 1.7 s */

typedef struct {
	uint16_t syncs;                      /* sync words found */
	uint16_t done;                       /* packets read to the end */
	uint16_t words;                      /* FIFO words read */
	uint16_t frames;                     /* good AX.25 frames */
	uint16_t bad;                        /* frames with a bad FCS */
	uint8_t  mode;                       /* 0 FFSK 1200/1800, 1 FFSK 1200/2400 */
	uint8_t  order;                      /* which of the four orders decoded the last frame (0-3), 0xFF none yet */
	uint16_t last[4];                    /* the first FIFO words of the last packet */
} FskRxStat_t;

void FSKRX_Start(uint8_t mode);
void FSKRX_Stop(void);
/* Every main-loop tick. Returns the length of an AX.25 frame just received (FCS left off) and sets *frame. */
unsigned FSKRX_Poll(const uint8_t **frame);
const FskRxStat_t *FSKRX_Stat(void);

#endif
