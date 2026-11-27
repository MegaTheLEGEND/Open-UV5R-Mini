/* Open-UV5R-Mini: AX.25 UI frames and their HDLC bit stream, for APRS (Sean asked, 2026-11-27).
 *
 * Written from the public AX.25 2.2 and APRS 1.01 specifications; no radio or other firmware code in it, so it is
 * tested on the PC (tests/aprs_host.py).
 *
 *   Frame    address fields (7 bytes each: 6 characters shifted left one bit, then the SSID byte; the last one has
 *            bit 0 set), control 0x03 (UI), PID 0xF0 (no layer 3), the information, FCS (CRC-16-CCITT, reflected,
 *            0xFFFF in, inverted out, low byte first).
 *   On air   flags 0x7E around the frame, bits low first, a 0 stuffed after five 1s, then NRZI: a 0 bit changes
 *            the tone, a 1 keeps it. For APRS the tones are Bell 202: 1200 Hz (mark) and 2200 Hz, 1200 bits/s.
 *
 * The receiver works on line levels (which tone, after any demodulator); NRZI makes it blind to which tone is
 * which, so the same decoder takes the BK4819's FSK bits or a software AFSK demodulator's.
 */
#ifndef RADIO_AX25_H
#define RADIO_AX25_H

#include <stdbool.h>
#include <stdint.h>

#define AX25_MAX_FRAME 332u              /* 10 addresses, control, PID, 256 information, FCS */
#define AX25_MIN_FRAME 18u               /* two addresses, control, one byte, FCS */

uint16_t AX25_Fcs(const uint8_t *p, unsigned n);

/* "N0CALL-9" -> "N0CALL" (upper case, at most 6 letters or digits) and 9; false if it isn't a call sign */
bool AX25_ParseCall(const char *s, char call[7], uint8_t *ssid);

/* A UI frame with its FCS, from text addresses: dst "APZUV5", src "N0CALL-9", path "WIDE1-1,WIDE2-1" (or "").
 * Returns the length, 0 if something doesn't fit or isn't a call sign. */
unsigned AX25_BuildUi(uint8_t *out, unsigned max, const char *dst, const char *src, const char *path,
                      const uint8_t *info, unsigned info_len);

/* A received frame (FCS already checked and left off) as text the way TNCs print it:
 * "N0CALL-9>APZUV5,WIDE1-1*:info". Characters outside printable ASCII in the information become '.'.
 * Returns the text length, 0 if the address fields are not valid. */
unsigned AX25_ToText(const uint8_t *frame, unsigned len, char *out, unsigned max);

/* ------------------------------------------------------------------ sending: the line level, bit by bit */
typedef struct {
	const uint8_t *p;
	uint16_t n, i;                       /* frame bytes, the next one */
	uint16_t pre, post;                  /* flags still to send before and after */
	uint8_t  byte, bit;                  /* the byte going out, the next bit of it (8 = fetch) */
	uint8_t  ones;                       /* 1s in a row (frame bits only) */
	bool     flag;                       /* the byte is a flag: not stuffed */
	bool     stuff;                      /* a 0 to stuff next */
	bool     level;                      /* the line: true = mark (1200 Hz) */
	bool     done;
} Ax25Tx_t;

/* pre: flags before the frame (the transmitter's start-up and the receivers' clock, "TXDELAY": 1200 bits/s, so
 * 8 flags = 53 ms), post: flags after */
void AX25_TxStart(Ax25Tx_t *t, const uint8_t *frame, unsigned len, unsigned pre, unsigned post);
/* the next bit's line level: 1 mark, 0 space; -1 when all sent */
int  AX25_TxNext(Ax25Tx_t *t);

/* ------------------------------------------------------------------ receiving: line levels in, frames out */
typedef struct {
	uint8_t  buf[AX25_MAX_FRAME];
	uint16_t n;                          /* bytes in buf */
	uint8_t  sr;                         /* the last 8 data bits, newest in bit 7 (flag = 0x7E) */
	uint8_t  byte, bits, ones;
	bool     last;                       /* the previous line level */
	bool     in;                         /* between flags */
	uint16_t frames, bad_fcs;            /* counts, for the screen */
} Ax25Rx_t;

void AX25_RxInit(Ax25Rx_t *r);
/* One bit's line level. Returns the length of a frame that just ended with a good FCS (in r->buf, FCS left off;
 * valid until the next call), else 0. */
unsigned AX25_RxBit(Ax25Rx_t *r, bool level);

#endif
