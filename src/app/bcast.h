/* Open-UV5R-Mini: broadcast FM in the VFO (ENABLE_APP_FM, release 1.0.0).
 *
 * The MCU's own FM receiver (platform/jl_sdk/fmrx.h, 64-108 MHz) plays the VFO's frequency through the speaker,
 * without the FM RADIO app, while the BK4819 is parked asleep (RADIO_Park). app.c decides when (the WFM bandwidth on
 * a frequency in the FM receiver's range, ROADMAP 3.13); this file only starts, tunes and stops it.
 *
 * BCAST_ClockCheck is shared with the FM app: while FM runs the CPU clock changes, so the timer's real rate is
 * measured against the FM interrupt and handed to the system tick (SYSTEM_SetTicksPerSec). */
#ifndef APP_BCAST_H
#define APP_BCAST_H

#include <stdbool.h>
#include <stdint.h>

#define BCAST_MIN 6400000u           /* 10 Hz units, as gVfo.freq: the FM receiver's range (FMRX_MIN_KHZ/MAX_KHZ) */
#define BCAST_MAX 10800000u
#define BCAST_AUTO_MIN 8750000u      /* tuning into 87.5-108 MHz picks WFM by itself (app.c) */

#if defined(APP_FM_ENABLE) && APP_FM_ENABLE
bool BCAST_Start(uint32_t freq, uint8_t volume);   /* freq in 10 Hz; false: it did not start (the BK4819 back) */
void BCAST_Tune(uint32_t freq);
void BCAST_Volume(uint8_t volume);                 /* 0-15 */
void BCAST_Stop(void);                             /* FM off, the key beeps' DAC and the BK4819 back */
bool BCAST_On(void);
void BCAST_Tick(void);                             /* every main-loop tick while on: clock rate, RDS */
int8_t BCAST_Bars(uint8_t max);                    /* the FM receiver's level as 0..max bars */
const char *BCAST_Name(void);                      /* the RDS station name (8 characters), or 0 when none yet */
void BCAST_ClockCheck(void);
#else                                /* no FM receiver in the build: never on */
static inline bool BCAST_Start(uint32_t f, uint8_t v) { (void)f; (void)v; return false; }
static inline void BCAST_Tune(uint32_t f) { (void)f; }
static inline void BCAST_Volume(uint8_t v) { (void)v; }
static inline void BCAST_Stop(void) {}
static inline bool BCAST_On(void) { return false; }
static inline void BCAST_Tick(void) {}
static inline int8_t BCAST_Bars(uint8_t m) { (void)m; return 0; }
static inline const char *BCAST_Name(void) { return 0; }
#endif

#endif
