/* Open-UV5R-Mini: the stock settings and VFOs, read from the external flash (read only).
 *
 * The stock app keeps both as logs: each change appends a slot, the last slot is the current one, and a slot
 * ends in a CRC-16/XMODEM (little-endian). Layout from Codeplug Studio's docs/reference/radios/baofeng/uv-5r-mini/
 * settings.md (traced from the stock app) and checked against one radio's own flash (all 24 VFO and 33 settings
 * slots pass their CRC):
 *
 *   VFO log      0x8010, slots of 0x42: VFO A (32 bytes), VFO B (32), CRC of the 64 bytes
 *   settings log 0x9010, slots of 0x60: 64 setting bytes, 30 zero bytes, CRC of the first 0x5E bytes
 *   (0x8000 and 0x9000 hold a 16-byte header each; meaning not known yet)
 *
 * VFO record: 0-7 frequency, one decimal digit a byte (MMM.MMMMM); 8-9 RX tone, 10-11 TX tone (app/tones.h);
 * 14 bits 6:5 shift (1 +, 2 -); 16 power (0 High, 1 Low); 17 bit 6 narrow; 18 band (the stock band index: 4 on
 * every airband slot in one radio's log, 0 elsewhere); 19 step index (the stock steps, RADIO_Steps; 0 = 2.5 kHz,
 * while on airband the stock app moved in 8.33 kHz steps); 20-26 offset, one digit a byte (MMM.MMMM).
 *
 * The stock app loads its defaults when the last slot's CRC is bad; so do we (SETTINGS_Load returns false).
 */
#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#define SETTINGS_VFO_LOG   0x8010u
#define SETTINGS_VFO_END   0x9000u
#define SETTINGS_VFO_SLOT  0x42u
#define SETTINGS_SET_LOG   0x9010u
#define SETTINGS_SET_END   0xA000u
#define SETTINGS_SET_SLOT  0x60u

/* Stock setting bytes we use (offsets in the 64-byte block). */
enum {
	SET_SQUELCH = 0x00, SET_POWER_SAVE = 0x01, SET_BACKLIGHT = 0x03, SET_DUAL_WATCH = 0x04, SET_TOT = 0x05,
	SET_BEEP = 0x06, SET_SCAN_MODE = 0x0A, SET_BCL = 0x0F, SET_AUTO_LOCK = 0x10, SET_ROGER = 0x17,
	SET_ACTIVE_SIDE = 0x18, SET_WORK_MODE = 0x1A, SET_KEY_LOCK = 0x1B,
};

typedef struct {
	uint32_t freq;           /* 10 Hz */
	uint32_t offset;         /* 10 Hz */
	int8_t   shift;          /* 0, +1, -1 */
	uint16_t rx_tone, tx_tone;
	uint8_t  power;          /* 0 High, 1 Low */
	bool     narrow;
	uint8_t  step;           /* index into RADIO_Steps */
} StockVfo_t;

typedef struct {
	uint8_t    raw[64];      /* the setting bytes as stock keeps them */
	StockVfo_t vfo[2];       /* A, B */
	bool       have_settings, have_vfo;
} StockSettings_t;

extern StockSettings_t gStock;

uint16_t SETTINGS_Crc16(const uint8_t *p, uint32_t n);
bool     SETTINGS_DecodeVfo(const uint8_t rec[32], StockVfo_t *v);
/* Read both logs. false if neither had a good last slot (then the defaults stand). */
bool     SETTINGS_Load(void);
/* Stock defaults for the setting bytes we use (settings.md), when the flash had none. */
void     SETTINGS_Defaults(void);

#endif
