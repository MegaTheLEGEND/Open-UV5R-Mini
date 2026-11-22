/* Open-UV5R-Mini: the radio core: one VFO, receive with squelch, transmit (guarded).
 *
 * Written for this radio from the stock app's hardware facts (docs/RE-NOTES.md, "Peripheral sweep"), with the
 * structure of the UV-K5 open firmware (DualTachyon) as a reference for how a radio firmware is laid out. Not tried
 * on a radio yet.
 */
#ifndef APP_RADIO_H
#define APP_RADIO_H

#include <stdbool.h>
#include <stdint.h>

/* Transmit is built in but off until someone has tried receive on a radio. Even when on, it only works inside the
 * amateur bands in radio.c, and only with the radio's own PA calibration (app/calib.h). */
#ifndef RADIO_ENABLE_TX
#define RADIO_ENABLE_TX 0
#endif

#define FREQ_MIN  1800000u    /* 18 MHz, in 10 Hz units */
#define FREQ_MAX 66000000u    /* 660 MHz */

typedef enum { MOD_AUTO = 0, MOD_FM, MOD_AM } Modulation_t;
typedef enum { RADIO_RX = 0, RADIO_RX_OPEN, RADIO_TX } RadioState_t;

typedef struct {
	uint32_t     freq;        /* 10 Hz units */
	uint8_t      step;        /* index into RADIO_Steps */
	uint8_t      squelch;     /* 0 (open) .. 9 */
	uint8_t      volume;      /* 0 .. 15 */
	bool         narrow;
	Modulation_t mod;
	uint16_t     rx_tone;     /* app/tones.h encoding (stock): 0 none, CTCSS 0.1 Hz, DCS index */
	uint16_t     tx_tone;
	uint8_t      power;       /* POWER_HIGH / POWER_LOW (app/calib.h) */
	uint32_t     tx_freq;     /* 10 Hz; 0 = same as freq */
	bool         rx_only;     /* a channel with no TX frequency */
	uint8_t      ptt_id;      /* channel byte 13: bit 0 send the up code at TX start, bit 1 the down code at the end */
} Vfo_t;

extern Vfo_t gVfo;
extern const uint16_t RADIO_Steps[];    /* 10 Hz units */
extern const uint8_t  RADIO_StepCount;

void         RADIO_Init(void);
void         RADIO_Apply(void);            /* after any change to gVfo */
void         RADIO_Tick(void);             /* every 10 ms: squelch */
bool         RADIO_ToneHeard(void);
char         RADIO_DtmfPoll(void);         /* a DTMF character heard, or 0 */        /* the receive tone is heard (true with no receive tone) */
RadioState_t RADIO_State(void);
uint16_t     RADIO_Rssi(void);             /* last raw RSSI (dBm = raw / 2 - 160) */
bool         RADIO_IsAm(void);
bool         RADIO_TxAllowed(void);
bool         RADIO_StartTx(void);          /* false if refused */
void         RADIO_EndTx(void);
void         RADIO_Beep(uint16_t hz, uint16_t ms);

/* Tones sent while transmitting (stock sub_01E510A2): code 0-15 = DTMF 0-9, A, B, C, D, *, #; 17-20 = one tone of
 * 1000, 1450, 1750 or 2100 Hz. Until RADIO_TxToneOff. monitor: heard locally with the mic left out (stock setting
 * 0x09 bit 0). Does nothing unless transmitting. */
bool         RADIO_TxTone(uint8_t code, bool monitor);
void         RADIO_TxToneOff(void);
void         RADIO_TxToneSilence(void);        /* between the codes of a sequence (generator left on) */
bool         RADIO_TxToneOn(void);
/* Roger beep at the end of transmit (stock sub_01E4DCC2): 1000 Hz then 850 Hz, 40 ms each, mic out. Blocks 80 ms. */
void         RADIO_Roger(void);
/* Squelch tail elimination (stock setting 0x14): the 55 Hz tail, or the DCS turn-off, until RADIO_EndTx. */
void         RADIO_TxTail(void);
/* Stock keeps transmitting 400 ms after PTT is let go (sub_01E4DCC2 sets a 40-tick count, 0x01E4FB9E waits for it). */
#define RADIO_TX_TAIL_MS 400u

#endif
