/* Open-UV5R-Mini: stock channel memory. See store/channels.h. */
#include "store/channels.h"
#include "store/settings.h"
#include "radio/tones.h"
#ifndef CHANNELS_HOST_TEST
#include "driver/extflash.h"
#else
void EXTFLASH_Read(uint32_t addr, uint8_t *buf, uint32_t len);
int  EXTFLASH_Erase4K(uint32_t addr);
int  EXTFLASH_Program(uint32_t addr, const uint8_t *buf, uint32_t len);
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

/* ------------------------------------------------------------------------------------------- writing */
static void put_bcd32(uint8_t *p, uint32_t v)
{
	for (int i = 0; i < 4; i++) {
		uint32_t two = v % 100u;
		p[i] = (uint8_t)((two / 10u) << 4 | two % 10u);
		v /= 100u;
	}
}

void CHANNEL_Encode(uint8_t rec[CHANNEL_SIZE], const Channel_t *ch, const uint8_t old[CHANNEL_SIZE])
{
	int i;

	for (i = 0; i < (int)CHANNEL_SIZE; i++)
		rec[i] = old && old[0] != 0xFF ? old[i] : 0x00;      /* bytes we don't model are kept (signal code etc.) */
	put_bcd32(rec, ch->rx);
	if (ch->tx)
		put_bcd32(rec + 4, ch->tx);
	else
		rec[4] = rec[5] = rec[6] = rec[7] = 0xFF;            /* receive only */
	rec[8] = (uint8_t)ch->rx_tone;
	rec[9] = (uint8_t)(ch->rx_tone >> 8);
	rec[10] = (uint8_t)ch->tx_tone;
	rec[11] = (uint8_t)(ch->tx_tone >> 8);
	rec[13] = (uint8_t)((rec[13] & ~3u) | (ch->ptt_id & 3u));
	rec[14] = (uint8_t)((rec[14] & ~3u) | (ch->low_power ? 1u : 0u));
	rec[15] = (uint8_t)((rec[15] & ~0x44u) | (ch->narrow ? 0x40u : 0u) | (ch->scan ? 0x04u : 0u));
	for (i = 0; i < 12; i++) {
		char c = ch->name[i];
		rec[20 + i] = (uint8_t)(c ? c : 0);
		if (!c) {
			for (i++; i < 12; i++)
				rec[20 + i] = 0;
			break;
		}
	}
}

#if SETTINGS_ENABLE_SAVE
/* One 4 KB sector holds 128 records. A record that only clears bits (an empty slot being filled) is programmed in
 * place; anything else reads the sector, erases it and writes it back, then reads the record back to check. */
static uint8_t s_sec[4096];

static int write_rec(uint16_t n, const uint8_t rec[CHANNEL_SIZE])
{
	uint32_t a = (uint32_t)n * CHANNEL_SIZE, base = a & ~0xFFFu;
	uint8_t old[CHANNEL_SIZE], back[CHANNEL_SIZE];
	bool clear_only = true;
	unsigned i;

	if (n >= CHANNEL_COUNT || a + CHANNEL_SIZE > 0x8000u)    /* never past the channel area */
		return -1;
	EXTFLASH_Read(a, old, sizeof old);
	for (i = 0; i < CHANNEL_SIZE; i++)
		if ((old[i] & rec[i]) != rec[i])
			clear_only = false;
	if (clear_only) {
		if (EXTFLASH_Program(a, rec, CHANNEL_SIZE))
			return -1;
	} else {
		EXTFLASH_Read(base, s_sec, sizeof s_sec);
		for (i = 0; i < CHANNEL_SIZE; i++)
			s_sec[a - base + i] = rec[i];
		if (EXTFLASH_Erase4K(base) || EXTFLASH_Program(base, s_sec, sizeof s_sec))
			return -1;
	}
	EXTFLASH_Read(a, back, sizeof back);
	for (i = 0; i < CHANNEL_SIZE; i++)
		if (back[i] != rec[i])
			return -1;
	return 0;
}

int CHANNEL_Save(uint16_t n, const Channel_t *ch)
{
	uint8_t old[CHANNEL_SIZE], rec[CHANNEL_SIZE];

	if (n >= CHANNEL_COUNT || !ch->rx)
		return -1;
	EXTFLASH_Read((uint32_t)n * CHANNEL_SIZE, old, sizeof old);
	CHANNEL_Encode(rec, ch, old);
	return write_rec(n, rec);
}

int CHANNEL_Delete(uint16_t n)
{
	uint8_t rec[CHANNEL_SIZE];

	for (unsigned i = 0; i < CHANNEL_SIZE; i++)
		rec[i] = 0xFF;                                       /* empty, as stock and CHIRP leave it */
	return write_rec(n, rec);
}
#endif
