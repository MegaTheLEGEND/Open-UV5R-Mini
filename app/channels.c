/* Open-UV5R-Mini: stock channel memory, read only. See app/channels.h. */
#include "app/channels.h"
#include "app/tones.h"
#ifndef CHANNELS_HOST_TEST
#include "driver/extflash.h"
#else
void EXTFLASH_Read(uint32_t addr, uint8_t *buf, uint32_t len);
#endif

static bool bcd32(const uint8_t *p, uint32_t *out)
{
	uint32_t v = 0;

	for (int i = 3; i >= 0; i--) {
		uint8_t hi = p[i] >> 4, lo = p[i] & 15u;
		if (hi > 9 || lo > 9)
			return false;
		v = v * 100u + hi * 10u + lo;
	}
	*out = v;
	return true;
}

static uint16_t tone(const uint8_t *p)
{
	return TONE_Normalise((uint16_t)(p[0] | p[1] << 8));
}

bool CHANNEL_Decode(const uint8_t rec[CHANNEL_SIZE], Channel_t *ch)
{
	int i;

	if (rec[0] == 0xFF || !bcd32(rec, &ch->rx) || ch->rx == 0)
		return false;
	if ((rec[4] & rec[5] & rec[6] & rec[7]) == 0xFF || !bcd32(rec + 4, &ch->tx))
		ch->tx = 0;
	else if (ch->tx == 0)
		ch->tx = ch->rx;                 /* 00 00 00 00 read as simplex */
	ch->rx_tone = tone(rec + 8);
	ch->tx_tone = tone(rec + 10);
	ch->low_power = (rec[14] & 3u) != 0;
	ch->ptt_id = rec[13] & 3u;
	ch->narrow = (rec[15] & 0x40u) != 0;
	ch->scan = (rec[15] & 0x04u) != 0;
	for (i = 0; i < 12 && rec[20 + i] && rec[20 + i] != 0xFF; i++)
		ch->name[i] = (char)(rec[20 + i] >= 0x20 && rec[20 + i] < 0x7F ? rec[20 + i] : '?');
	ch->name[i] = 0;
	return true;
}

bool CHANNEL_Load(uint16_t n, Channel_t *ch)
{
	uint8_t rec[CHANNEL_SIZE];

	if (n >= CHANNEL_COUNT)
		return false;
	EXTFLASH_Read((uint32_t)n * CHANNEL_SIZE, rec, sizeof rec);
	return CHANNEL_Decode(rec, ch);
}

/* Only the RX frequency is read while searching: an empty or invalid slot shows in its first four bytes. */
int CHANNEL_Next(int n, int dir)
{
	for (unsigned k = 1; k <= CHANNEL_COUNT; k++) {
		int m = (n + dir * (int)k) % (int)CHANNEL_COUNT;
		uint8_t f[4];
		uint32_t v;
		if (m < 0)
			m += (int)CHANNEL_COUNT;
		EXTFLASH_Read((uint32_t)m * CHANNEL_SIZE, f, sizeof f);
		if (f[0] != 0xFF && bcd32(f, &v) && v)
			return m;
	}
	return -1;
}
