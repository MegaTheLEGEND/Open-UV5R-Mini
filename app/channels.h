/* Open-UV5R-Mini: the stock channel memory, external flash 0x0000, read only.
 *
 * 999 records of 32 bytes (the clone image's channel record, as Codeplug Studio's docs/reference/radios/baofeng/
 * uv-5r-mini/channel-record.md describes it; it matches a radio's own flash):
 *   0-3 RX frequency, 4-7 TX frequency: little-endian BCD in 10 Hz units; TX FF FF FF FF = receive only
 *   8-9 RX tone, 10-11 TX tone: u16; 0 or 0xFFFF none; >= 0x258 CTCSS in 0.1 Hz; else a DCS index (app/tones.h)
 *   14 bits 0-1: 0 = High power, else Low
 *   15 bit 6: 1 = narrow; bit 2: scan
 *   20-31 name, ASCII, ends at 0x00 or 0xFF
 *   empty: first byte 0xFF
 */
#ifndef APP_CHANNELS_H
#define APP_CHANNELS_H

#include <stdbool.h>
#include <stdint.h>

#define CHANNEL_COUNT 999u
#define CHANNEL_SIZE  32u

typedef struct {
	uint32_t rx, tx;          /* 10 Hz; tx 0 = receive only */
	uint16_t rx_tone, tx_tone;    /* app/tones.h encoding; 0 = none */
	bool     low_power, narrow, scan;
	char     name[13];
} Channel_t;

/* Decode one record; false if the slot is empty or the frequency isn't valid BCD. */
bool CHANNEL_Decode(const uint8_t rec[CHANNEL_SIZE], Channel_t *ch);
/* Read and decode slot n (0-based) from the external flash. */
bool CHANNEL_Load(uint16_t n, Channel_t *ch);
/* Next / previous occupied slot from n (wrapping), or -1 if none. */
int  CHANNEL_Next(int n, int dir);

#endif
