/* Open-UV5R-Mini: sub-audio tones (CTCSS and DCS) in the stock app's own encoding.
 *
 * A tone is a u16, the same value the stock channel record (bytes 8-11) and VFO record keep:
 *   0 or 0xFFFF       none
 *   1 .. 105          DCS, normal: code = TONE_DcsCodes[n - 1]
 *   106 .. 210        DCS, inverted: code = TONE_DcsCodes[n - 106]
 *   >= 0x258 (600)    CTCSS in 0.1 Hz (885 = 88.5 Hz)
 * Stock: the index decode at 0x01E4DE18 (table 0x01E667D0, 105 codes: the 104 standard ones and 645), the CTCSS
 * table at 0x01E662A4 (51 tones, with 63.0 Hz and without 150.0 Hz).
 */
#ifndef APP_TONES_H
#define APP_TONES_H

#include <stdint.h>

typedef enum { TONE_NONE = 0, TONE_CTCSS, TONE_DCS_N, TONE_DCS_I } ToneKind_t;

#define TONE_DCS_COUNT   105u
#define TONE_CTCSS_COUNT 51u

extern const uint16_t TONE_DcsCodes[TONE_DCS_COUNT];    /* the code as a number: D023 = 023 = 0x13 */
extern const uint16_t TONE_Ctcss[TONE_CTCSS_COUNT];     /* 0.1 Hz */

ToneKind_t TONE_Kind(uint16_t t);
uint16_t   TONE_Normalise(uint16_t t);                  /* anything unknown becomes 0 */
uint16_t   TONE_DcsCode(uint16_t t);                    /* the code (023 for D023), 0 if not DCS */
uint32_t   TONE_Golay(uint16_t code);                   /* the 23-bit DCS word: code | 0x800, 11 check bits above */
uint16_t   TONE_Step(uint16_t t, int dir);              /* OFF, the CTCSS tones, DCS normal, DCS inverted, OFF */
char      *TONE_Format(char *b, uint16_t t);            /* "OFF", "88.5", "D023N", "D023I"; b >= 7 bytes */
/* Tone scan results to a tone. CTCSS: the table tone within 0.9 Hz (stock CTCSS_FindNearest, 0x01E4ED34);
 * DCS: the code whose word, in any of its 23 rotations, is the word heard (stock DCS_FindCode, 0x01E4EE64),
 * or its inverse (then an inverted code). 0 if none. */
uint16_t   TONE_FromCtcss(uint32_t tenths);
uint16_t   TONE_FromDcsWord(uint32_t word);

#endif
