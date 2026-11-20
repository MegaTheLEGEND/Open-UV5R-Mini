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

void     BK4819_Init(void);
uint16_t BK4819_ReadRegister(uint8_t reg);
void     BK4819_WriteRegister(uint8_t reg, uint16_t value);

void     BK4819_SetMode(BK4819_Mode_t mode);
void     BK4819_SetFrequency(uint32_t freq_10hz);
uint16_t BK4819_GetRSSI(void);
BK4819_CssScanResult_t BK4819_GetCxCSSScanResult(uint32_t *cdcss, uint16_t *ctcss);

#endif
