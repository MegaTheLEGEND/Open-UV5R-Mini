/* Open-UV5R-Mini: FM broadcast receiver (ENABLE_APP_FM), 64-108 MHz on the AC695N's own FM block, played through
 * the MCU's audio DAC.
 *
 * JieLi's FM library (fm_inside, from the public BR23 SDK, linked from build/sdk/; docs/THIRD-PARTY.md) does the
 * radio part: calibration, tuning, and the demodulation in its interrupt (43), which hands us 32 stereo frames at
 * 37.5 kHz every 0.85 ms. We turn them to mono, resample to 48 kHz and write them into the DAC's buffer; JieLi's DAC
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
	uint32_t irqs;           /* FM interrupts so far (about 1172 a second when running) */
	uint32_t frames;         /* 37.5 kHz frames received */
	uint32_t dac_out;        /* 48 kHz samples written to the DAC */
	uint32_t dac_drop;       /* samples dropped: the DAC buffer was full */
	uint16_t peak;           /* largest sample in the last FM buffer, before the volume */
	uint32_t heap_peak;      /* bytes */
	uint32_t asserts;        /* JieLi library asserts / reset requests (counted, not acted on) */
	uint32_t sys_div;        /* clock registers, to see the CPU speed: 0x1E0008, 0x1E000C */
	uint32_t clk_con0;
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

#define FMRX_DAC 1u              /* the DAC and speaker */
#define FMRX_RX  2u              /* the FM receiver */
bool     FMRX_Open(uint32_t khz, uint32_t mode);   /* FMRX_DAC | FMRX_RX for the radio */
void     FMRX_ToneFill(void);    /* DAC only: keep a 1 kHz test tone in the DAC buffer (call every tick) */
void     FMRX_Close(void);
bool     FMRX_IsOpen(void);
bool     FMRX_Tune(uint32_t khz);       /* true: the library thinks a station is there */
int      FMRX_Rssi(void);               /* the library's level, as it gives it */
bool     FMRX_Stereo(void);
void     FMRX_SetVolume(uint8_t v);     /* 0..15 */
void     FMRX_Stats(FmrxStats_t *s);

#endif
