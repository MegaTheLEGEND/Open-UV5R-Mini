/* Open-UV5R-Mini: RDS (Radio Data System) receive for the FM broadcast radio.
 *
 * Our own code, from the public RDS standard (IEC 62106 / NRSC-4-B): 1187.5 bit/s, biphase-coded, differentially
 * coded BPSK on a suppressed 57 kHz subcarrier locked to the 19 kHz stereo pilot; 26-bit blocks (16 data bits and a
 * 10-bit check word, generator x^10+x^8+x^7+x^5+x^4+x^3+1, plus an offset word A, B, C, C' or D); four blocks a
 * group. tools/rds_decode.py is the same decoder on the PC.
 *
 * Two halves:
 *   RDS_FeBlock  in the FM interrupt, on the raw demodulated FM signal (the "MPX", about 600 000 samples a second,
 *                512 a call). A phase-locked loop follows the 19 kHz pilot; its phase times 3 is the 57 kHz carrier.
 *                The signal mixed down is summed over every 2 pilot cycles, so 8 values a bit, in step with the bits
 *                (the bit rate is the pilot / 16). Cheap: a few multiplies a sample.
 *   RDS_Poll     in the main loop: picks the carrier phase (8 angles) and the bit timing (8 offsets) with the most
 *                energy, the biphase bits, differential decoding, block sync with error correction (bursts up to
 *                5 bits), then the PI code, the station name (PS, groups 0A/0B) and radio text (RT, 2A/2B).
 *
 * Needs a stereo station: without the pilot there is no carrier reference (mono stations with RDS are not decoded).
 */
#ifndef RADIO_RDS_H
#define RADIO_RDS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	bool     pilot;                      /* the pilot loop is locked */
	bool     sync;                       /* block sync */
	uint16_t pi;                         /* programme identification (0 = none yet) */
	char     ps[9];                      /* station name, 8 characters, spaces until heard */
	char     rt[65];                     /* radio text, up to 64 characters */
	char     ptyn[9];                    /* programme type name (10A), spaces until heard */
	uint8_t  pty;                        /* programme type, 0-31 (RBDS names in RDS_PtyName) */
	bool     tp, ta, ms;                 /* traffic programme, traffic announcement now, music (else speech) */
	uint8_t  di;                         /* decoder identification bits, as heard (bit 0 stereo, 3 dynamic PTY) */
	uint8_t  ecc;                        /* extended country code (1A variant 0), 0 = none */
	uint8_t  naf;                        /* alternative frequencies heard (0A), in 100 kHz above 87.5 MHz */
	uint8_t  af[25];
	bool     ct;                         /* clock time heard (4A) */
	uint16_t ct_year;                    /* UTC */
	uint8_t  ct_month, ct_day, ct_hour, ct_min;
	int8_t   ct_offset;                  /* local time offset, half hours */
	uint16_t group_seen;                 /* group types seen: bit n = type n (A or B) */
	uint32_t groups;                     /* groups decoded */
	uint32_t blocks_ok, blocks_bad;      /* blocks with sync: good (after correction), bad */
} RdsInfo_t;

/* fs_mpx: the raw FM sample rate (samples a second), for the pilot loop's starting frequency */
void RDS_Reset(uint32_t fs_mpx);
/* the FM interrupt: n raw samples (sign-extended, as JieLi's FM block writes them) */
void RDS_FeBlock(const volatile int32_t *x, uint32_t n);
/* the main loop: decode what the interrupt gathered; true when something shown changed */
bool RDS_Poll(void);
const RdsInfo_t *RDS_Info(void);
/* the programme type's name, RBDS (North American) list, up to 8 characters */
const char *RDS_PtyName(uint8_t pty);
/* a US PI code's call sign ("KZZU"), or "" when it is not one */
void RDS_CallSign(uint16_t pi, char out[5]);

#endif
