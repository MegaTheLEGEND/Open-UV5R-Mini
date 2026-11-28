/* Open-UV5R-Mini: FM broadcast receiver (ENABLE_APP_FM), 64-108 MHz on the AC695N's own FM block, played through
 * the MCU's audio DAC.
 *
 * JieLi's FM library (fm_inside, from the public BR23 SDK, linked from build/sdk/; docs/THIRD-PARTY.md) does the
 * radio part: calibration, tuning, and the demodulation in its interrupt (43), which hands us 32 stereo frames at
 * 41.7 kHz every 0.77 ms. We turn them to mono, resample to 48 kHz and write them into the DAC's buffer; JieLi's DAC
 * driver (audio_dac) only sets the DAC up (power, trim, rate). Board settings for the DAC are stock's own (read from
 * the stock app in the emulator, 2026-11-24): right channel only, DAC LDO level 2, VCMO on.
 *
 * Nothing here has run on a radio yet. FMRX_Stats() gives the counters the FM screen shows, to see where it
 * stops if it does. */
#ifndef PLATFORM_FMRX_H
#define PLATFORM_FMRX_H

#include <stdbool.h>
#include <stdint.h>

#define FMRX_MIN_KHZ  64000u
#define FMRX_MAX_KHZ 108000u

typedef struct {
	uint32_t irqs;           /* FM interrupts so far (about 1302 a second when running) */
	uint32_t frames;         /* 41.7 kHz frames received */
	uint32_t dac_out;        /* 48 kHz samples written to the DAC */
	uint32_t dac_drop;       /* samples dropped: the DAC buffer was full */
	uint16_t peak;           /* largest sample in the last FM buffer, before the volume */
	uint32_t heap_peak;      /* bytes */
	uint32_t asserts;        /* JieLi library asserts / reset requests (counted, not acted on) */
	uint32_t sys_div;        /* clock registers, to see the CPU speed: 0x1E0008, 0x1E000C */
	uint32_t clk_con0;
	uint32_t adc_calls;      /* ADC conversions the FM calibration asked for, and how many never finished */
	uint32_t adc_timeouts;
	uint32_t irq_hz;         /* FM interrupts a second, timed by the crystal (timer 3, 24 MHz) */
	uint32_t main_pct;       /* CPU time left for everything else, % (a fixed loop timed with and without interrupts) */
	uint8_t  step;           /* where FMRX_Open got to (0 = not opened, 9 = all done) */
} FmrxStats_t;

/* Breadcrumbs for the first tries on a radio: kept in RAM that start-up does not clear (.ram2), so after a reset
 * the FM screen can show how far the last start got. step: where FMRX_Open was (codes in fmrx.c); glue: the last
 * call JieLi's code made into ours (fm_glue.c); exc: an exception (vector 0-2) and the address it happened at. */
#define FMRX_CRUMB_MAGIC 0x464D3031u      /* "FM01" */
typedef struct {
	uint32_t magic;
	uint32_t step;
	uint32_t glue;
	uint32_t irqs;           /* FM interrupts seen */
	uint32_t exc;            /* 0 none, else vector + 1 */
	uint32_t exc_pc;         /* reti: where it happened */
	uint32_t exc_rets;       /* rets: who called that function */
	uint32_t mode;
} FmrxCrumb_t;
extern volatile FmrxCrumb_t gFmCrumb;
/* Slow-motion start: when set, called at every breadcrumb step and at the rarer library calls (request_irq, ADC
 * start, assert, reset request) with the step and the call; the FM app shows them on the screen and waits a little,
 * so a start that dies leaves its last step on the screen (Sean's second try, 2026-11-24: the RAM breadcrumbs of the
 * crashed start never arrived). */
extern void (*gFmTrace)(uint32_t step, uint32_t glue);
/* Heartbeat during a slow-motion start, with no wait: at every ADC call after the first two and every 16th delay
 * call, so a start that hangs shows whether the calibration is still measuring (ADC count climbing) or looping
 * somewhere else (only the delay count climbing). Sean's fifth try, 2026-11-24: it stopped at "71 G3023". */
extern void (*gFmBeat)(void);         /* not from the FM interrupt any more (it drew on the screen while the main code did), was every 64th (Sean's seventh try, 2026-11-24:
                                         * the start stopped right after fm_hw_init turns the FM block on, at
                                         * 0x01E0DEDC in that build, so: does its interrupt take all the time?) */
extern uint32_t gFmAdcCalls, gFmAdcTimeouts, gFmDelays;
extern uint32_t gFmDelayFrom;            /* where the last delay() call came from (its return address) */
/* An exception (vector 0-2) during a slow-motion start: shown on the screen, then the radio waits there (watchdog
 * fed) until it is switched off. Sean's sixth try, 2026-11-24: it stopped at A26 T0 D1968, with no more calls into
 * our code, which looks like an exception or a loop in the library. */
extern void (*gFmExc)(uint32_t vec, uint32_t pc, uint32_t rets);
/* Main code stalled during a start (no library delay call for about 0.5 s of FM interrupts): called from the FM
 * interrupt with where the main code was interrupted (reti), the interrupt count and the delay count. The same
 * address is kept in the breadcrumbs (exc_rets, the second PC number) at every interrupt, so it survives a restart.
 * Sean's thirteenth try (2026-11-25): at 120 MHz the start still stopped right after the FM block is turned on. */
extern void (*gFmStall)(uint32_t main_pc, uint32_t irqs, uint32_t delays);
/* With it, timed on the crystal, in microseconds, averaged since the last call: P = time between FM interrupts,
 * G = from the end of our handler to the start of the next (JieLi's demodulator and the interrupt's way in and
 * out, plus whatever main code ran), H = our handler. If P is far below 853 us the interrupt comes too often; if
 * G + H is about P the interrupt itself takes all the time (Sean's fifteenth try, 2026-11-25: the radio clocks set
 * as stock, 120 MHz, and the main code still stalled once FM ran). */
extern uint32_t gFmP_us, gFmG_us, gFmH_us;
/* Sean's sixteenth try (2026-11-25): P 240 G 229 H 10, so the FM interrupt comes 3.55 times too often (853 us
 * expected) and takes all the time. Its sample clock is wrong: the clock and PLL registers are shown with it now,
 * and the CPU's real speed (a fixed loop timed on the crystal before and after the clock change, crystal ticks). */
extern uint32_t gFmLoop48, gFmLoopFast;
extern uint32_t gFmClkCon1Was;           /* CLK_CON1 as the bootloader left it, before FMRX_Open set its radio clocks */

#define FMRX_DAC 1u              /* the DAC and speaker */
#define FMRX_RX  2u              /* the FM receiver */
/* 120 MHz (from 48) while the receiver runs, as stock's own FM open sets it (fmrx.c clock_set). Needed: measured in the
 * emulator (emu/fm_cost.py, 2026-11-25), one FM interrupt is about 24 000 instructions (JieLi's fm_dem 22 600, our
 * handler 1 500), 1172 a second = 28 million instructions a second, more than 48 MHz gives once loads and
 * multiplies are counted; on Sean's radio at 48 MHz the interrupt left the rest no time at all. */
#define FMRX_FAST 4u
uint32_t FMRX_SysMHz(void);      /* the CPU clock now, from the clock registers */
/* the FM radio's fast clock (160 MHz, flash divider / 1), also for the Bluetooth controller (bt_le.c); back = as before */
void     FMRX_ClockFast(void);
void     FMRX_ClockBack(void);
bool     FMRX_Open(uint32_t khz, uint32_t mode);   /* FMRX_DAC | FMRX_RX for the radio */
void     FMRX_ToneFill(void);    /* DAC only: keep a 1 kHz test tone in the DAC buffer (call every tick) */
/* Key beeps on the DAC. FMRX_BeepPrepare opens it for them (slow: at power-on, behind the picture) unless the FM
 * radio has it; FMRX_BeepOff closes it (the radio asleep). FMRX_Beep: false = no DAC now (the FM radio has it, or it
 * didn't open), so make the beep another way. FMRX_BeepTick every tick. */
bool     FMRX_BeepPrepare(void);
void     FMRX_BeepOff(void);
bool     FMRX_Beep(uint16_t hz, uint16_t ms);
void     FMRX_BeepTick(uint16_t dt_ms);
bool     FMRX_Beeping(void);
void     FMRX_Close(void);
bool     FMRX_IsOpen(void);
/* true when the receiver was stopped because its interrupt came far too often (FMRX_Open then returns false):
 * on Sean's radio (2026-11-25) it came every 240 us instead of 853 and left no time for the keys, the screen or the
 * power switch, so it is switched off rather than freezing the radio */
bool     FMRX_TooFast(void);
bool     FMRX_Tune(uint32_t khz);       /* true: the library thinks a station is there */
int      FMRX_Rssi(void);               /* the library's level, as it gives it */
bool     FMRX_Stereo(void);
void     FMRX_SetVolume(uint8_t v);     /* 0..15 */
void     FMRX_Stats(FmrxStats_t *s);

#endif
