/* Open-UV5R-Mini: BK4819 RF transceiver driver. */
#ifndef DRIVER_BK4819_H
#define DRIVER_BK4819_H

#include <stdbool.h>
#include <stdint.h>
#include "driver/bk4819-regs.h"

/* Modes of REG_30 as switched by the stock firmware (sub_01E3FC64). */
typedef enum {
	BK4819_MODE_OFF = 0,
	BK4819_MODE_RX  = 1, /* REG_30 = 0xBFF1 */
	BK4819_MODE_TX  = 2, /* REG_30 = 0xC1FE */
	BK4819_MODE_3   = 3, /* REG_30 = 0x0302, purpose unknown */
	BK4819_MODE_TONE = 4, /* REG_30 = 0xC3FA, assumed tone/DTMF TX */
} BK4819_Mode_t;

typedef enum {
	BK4819_CSS_NONE = 0,
	BK4819_CSS_CTCSS,
	BK4819_CSS_CDCSS,
} BK4819_CssScanResult_t;

typedef enum {
	BK4819_AF_MUTE = 0,
	BK4819_AF_FM   = 1,
	BK4819_AF_BEEP = 3,
	BK4819_AF_AM   = 6,  /* stock value on this radio (0x6640) */
} BK4819_AF_t;

typedef enum { BK4819_TONE_OFF = 0, BK4819_TONE_CTCSS, BK4819_TONE_DCS_N, BK4819_TONE_DCS_I } BK4819_Tone_t;

/* REG_02 / REG_3F interrupt bits */
#define BK4819_INT_SQUELCH_LOST  (1u << 2)
#define BK4819_INT_SQUELCH_FOUND (1u << 3)
#define BK4819_INT_CTCSS_LOST    (1u << 8)
#define BK4819_INT_CTCSS_FOUND   (1u << 9)

/* BK4819 GPIO masks the stock band switch uses (sub_01E41382) */
#define BK4819_GPIO_TX_VHF 0x08u
#define BK4819_GPIO_TX_UHF 0x10u
#define BK4819_GPIO_RX_UHF 0x20u

void     BK4819_Init(void);
uint16_t BK4819_ReadRegister(uint8_t reg);
void     BK4819_WriteRegister(uint8_t reg, uint16_t value);

void     BK4819_SetMode(BK4819_Mode_t mode);
void     BK4819_SetFrequency(uint32_t freq_10hz);
uint16_t BK4819_GetRSSI(void);
BK4819_CssScanResult_t BK4819_GetCxCSSScanResult(uint32_t *cdcss, uint16_t *ctcss);

void     BK4819_SetAF(BK4819_AF_t af);
void     BK4819_SetVolume(uint8_t level);            /* 0..15 */
void     BK4819_SetFilterBandwidth(bool narrow);
void     BK4819_SetGpio(uint8_t mask, bool on);
void     BK4819_SetupSquelch(uint8_t open_rssi, uint8_t close_rssi, uint8_t open_noise, uint8_t close_noise,
                             uint8_t open_glitch, uint8_t close_glitch);
void     BK4819_SetInterrupts(uint16_t mask);
uint16_t BK4819_TakeInterrupts(void);
void     BK4819_RxOn(void);
void     BK4819_Sleep(void);
void     BK4819_SetCtcss(uint16_t tenths, uint8_t level);
void     BK4819_SetDcs(uint32_t word, bool inverted, uint8_t level);
void     BK4819_CssOff(void);
void     BK4819_DtmfRxOn(void);
void     BK4819_DtmfRxOff(void);
int      BK4819_DtmfTake(void);                      /* 0-15 or -1 */
uint16_t BK4819_ToneWord(uint16_t hz);               /* REG_71 / REG_72 value */
void     BK4819_TailTone(bool dcs, uint8_t level);  /* squelch tail elimination at TX end */
void     BK4819_TxTonesOn(bool dual);                /* dual: DTMF generator; else one tone */
void     BK4819_TxTonePair(uint16_t w1, uint16_t w2);
void     BK4819_TxTonesOff(void);
void     BK4819_TxMonitor(bool on);                  /* tone heard locally, mic out (stock TX mode 4) */
void     BK4819_CssScanStart(void);
/* BK4819_TONE_DCS_N: *value = the 23-bit word heard; BK4819_TONE_CTCSS: *value = 0.1 Hz; BK4819_TONE_OFF: nothing */
BK4819_Tone_t BK4819_CssScanRead(uint32_t *value);
bool     BK4819_CssHeard(BK4819_Tone_t kind);
void     BK4819_SetPa(uint8_t bias, uint8_t gain);
void     BK4819_TxMute(bool on);
void     BK4819_SetMicGain(uint8_t gain);           /* 0..31 */
void     BK4819_TxOn(void);
void     BK4819_Beep(uint16_t hz);
uint8_t  BK4819_GetNoise(void);

#endif
