/* Open-UV5R-Mini: the stock settings and VFOs, read from the external flash (read only).
 *
 * The stock app keeps both as logs: each change appends a slot, the last slot is the current one, and a slot
 * ends in a CRC-16/XMODEM (little-endian). Layout from Codeplug Studio's docs/reference/radios/baofeng/uv-5r-mini/
 * settings.md (traced from the stock app) and checked against one radio's own flash (all 24 VFO and 33 settings
 * slots pass their CRC):
 *
 *   VFO log      0x8010, slots of 0x42: VFO A (32 bytes), VFO B (32), CRC of the 64 bytes
 *   settings log 0x9010, slots of 0x60: 64 setting bytes, 30 zero bytes, CRC of the first 0x5E bytes
 *   0x8000 and 0x9000: the slot bitmap of each log (stock sub_01E40386): one bit a slot, cleared once the slot is
 *   written and superseded; the first bit still set is the current slot.
 *
 * VFO record: 0-7 frequency, one decimal digit a byte (MMM.MMMMM); 8-9 RX tone, 10-11 TX tone (radio/tones.h);
 * 14 bits 6:5 shift (1 +, 2 -); 16 power (0 High, 1 Low); 17 bit 6 narrow; 18 not traced (4 on every airband slot
 * in one radio's log, 0 on all others); 19 step index (the stock steps, RADIO_Steps; 0 = 2.5 kHz,
 * while on airband the stock app moved in 8.33 kHz steps); 20-26 offset, one digit a byte (MMM.MMMM).
 *
 * The stock app loads its defaults when the last slot's CRC is bad; so do we (SETTINGS_Load returns false).
 */
#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

/* Saving (writing the two logs back, as stock; and CHIRP programming, app/program.c). On since 2026-11-22 (Sean asked
 * for CHIRP support and saving). With 0 there is no erase or program code for the external flash in the image at
 * all (driver/extflash.c); with 1 writes are fenced to 0x0000-0xAFFF, never the calibration at 0xF000. */
#ifndef SETTINGS_ENABLE_SAVE
#define SETTINGS_ENABLE_SAVE 1
#endif

#define SETTINGS_VFO_HDR     0x8000u   /* slot bitmap, 8 bytes (stock reads 8) */
#define SETTINGS_VFO_HDR_LEN 8u
#define SETTINGS_VFO_LOG     0x8010u
#define SETTINGS_VFO_SLOT    0x42u
#define SETTINGS_VFO_MAX     59u       /* current slot past this: erase and start again (stock) */
#define SETTINGS_SET_HDR     0x9000u   /* 6 bytes */
#define SETTINGS_SET_HDR_LEN 6u
#define SETTINGS_SET_LOG     0x9010u
#define SETTINGS_SET_SLOT    0x60u
#define SETTINGS_SET_MAX     40u

/* Stock setting bytes we use (offsets in the 64-byte block). */
enum {
	SET_SQUELCH = 0x00, SET_POWER_SAVE = 0x01, SET_BACKLIGHT = 0x03, SET_DUAL_WATCH = 0x04, SET_TOT = 0x05,
	SET_BEEP = 0x06, SET_DTMF_MON = 0x09 /* sidetone: bit 0 keys, bit 1 PTT-ID */, SET_SCAN_MODE = 0x0A, SET_PTT_ID = 0x0B /* VFO mode */, SET_DISP_A = 0x0D, SET_DISP_B = 0x0E, SET_BCL = 0x0F, SET_AUTO_LOCK = 0x10, SET_STE = 0x14, SET_RPT_CLEAR = 0x15, SET_ROGER = 0x17,
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
	uint8_t    vfo_raw[64];  /* the current VFO slot as read (bytes we don't model are written back unchanged) */
	StockVfo_t vfo[2];       /* A, B */
	bool       have_settings, have_vfo;
	/* External 0xA000 (stock reads 11 bytes into RAM at boot, sub_01E46F3E): 0-4 own ID codes (0xFF ends), 5 ?,
	 * 6 PTT-ID (bit 0 up code, bit 1 down code), 7 code time, 8 gap time (radio/dtmfseq.h), 9 separator (0-5 = A, B,
	 * C, D, *, #), 10 group call code (0-6 = none, A-D, *, #). Names as CHIRP's UV-5R Mini driver. */
	uint8_t    ani[11];
	uint8_t    up_code[16], down_code[16];   /* external 0xA180 / 0xA190, codes 0-15, 0xFF ends */
} StockSettings_t;

#define SETTINGS_ANI      0xA000u
#define SETTINGS_UP_CODE  0xA180u
#define SETTINGS_DOWN_CODE 0xA190u

extern StockSettings_t gStock;

uint16_t SETTINGS_Crc16(const uint8_t *p, uint32_t n);
bool     SETTINGS_DecodeVfo(const uint8_t rec[32], StockVfo_t *v);
void     SETTINGS_EncodeVfo(uint8_t rec[32], const StockVfo_t *v);
#if SETTINGS_ENABLE_SAVE
/* Write gStock.raw / gStock.vfo[] as a new slot of their log. 0 = written or unchanged, -1 = failed. */
int      SETTINGS_SaveSettings(void);
int      SETTINGS_SaveVfos(void);
/* Write a whole VFO record pair (A, B; 64 bytes as CHIRP sends them) as a new slot, then load it into gStock. */
int      SETTINGS_SaveVfoRaw(const uint8_t rec[64]);
#endif
/* Read both logs. false if neither had a good last slot (then the defaults stand). */
bool     SETTINGS_Load(void);
/* Stock defaults for the setting bytes we use (settings.md), when the flash had none. */
void     SETTINGS_Defaults(void);

#endif
