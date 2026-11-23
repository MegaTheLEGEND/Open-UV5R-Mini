/* Open-UV5R-Mini: the stock settings and VFOs, read only. See store/settings.h. */
#include "store/settings.h"
#include "radio/tones.h"
#ifndef CHANNELS_HOST_TEST
#include "driver/extflash.h"
#else
void EXTFLASH_Read(uint32_t addr, uint8_t *buf, uint32_t len);
int  EXTFLASH_Erase4K(uint32_t addr);
int  EXTFLASH_Program(uint32_t addr, const uint8_t *buf, uint32_t len);
#endif

StockSettings_t gStock;

uint16_t SETTINGS_Crc16(const uint8_t *p, uint32_t n)      /* CRC-16/XMODEM */
{
	uint16_t crc = 0;

	while (n--) {
		crc ^= (uint16_t)(*p++ << 8);
		for (int i = 0; i < 8; i++)
			crc = (uint16_t)(crc & 0x8000u ? (uint32_t)crc << 1 ^ 0x1021u : (uint32_t)crc << 1);
	}
	return crc;
}

static bool digits(const uint8_t *p, int n, uint32_t *out)
{
	uint32_t v = 0;

	for (int i = 0; i < n; i++) {
		if (p[i] > 9)
			return false;
		v = v * 10u + p[i];
	}
	*out = v;
	return true;
}

bool SETTINGS_DecodeVfo(const uint8_t rec[32], StockVfo_t *v)
{
	uint32_t off;

	if (!digits(rec, 8, &v->freq) || v->freq == 0)
		return false;
	v->offset = digits(rec + 20, 7, &off) ? off * 10u : 0;
	v->shift = (rec[14] >> 5 & 3u) == 1 ? 1 : (rec[14] >> 5 & 3u) == 2 ? -1 : 0;
	v->rx_tone = TONE_Normalise((uint16_t)(rec[8] | rec[9] << 8));
	v->tx_tone = TONE_Normalise((uint16_t)(rec[10] | rec[11] << 8));
	v->power = rec[16] ? 1 : 0;
	v->narrow = (rec[17] & 0x40u) != 0;
	v->step = rec[19] <= 8 ? rec[19] : 4;
	return true;
}

/* The current slot of a log, as stock sub_01E40386 finds it: the header is a bitmap, one bit a slot, bit 0 of
 * byte 0 first; written slots have their bit cleared, and the first bit still set is the current slot. */
static unsigned current_index(uint32_t header, uint32_t header_len)
{
	uint8_t h[8];

	EXTFLASH_Read(header, h, header_len);
	for (unsigned i = 0; i < header_len * 8u; i++)
		if (h[i >> 3] >> (i & 7u) & 1u)
			return i;
	return 0xFFu;
}

static bool read_current(uint32_t header, uint32_t header_len, uint32_t first, uint32_t size, unsigned max,
                         uint8_t *buf)
{
	unsigned i = current_index(header, header_len);

	if (i > max + 1u)            /* stock writes slot max + 1 while the current one is at max */
		return false;
	EXTFLASH_Read(first + i * size, buf, size);
	return true;
}

void SETTINGS_Defaults(void)
{
	static const uint8_t D[][2] = {
		{ SET_SQUELCH, 3 }, { SET_POWER_SAVE, 1 }, { SET_BACKLIGHT, 3 }, { SET_DUAL_WATCH, 1 }, { SET_TOT, 8 },
		{ SET_BEEP, 1 }, { SET_SCAN_MODE, 1 }, { SET_BCL, 0 }, { SET_AUTO_LOCK, 0 }, { SET_ROGER, 0 },
		{ SET_ACTIVE_SIDE, 1 }, { SET_WORK_MODE, 0x11 }, { SET_KEY_LOCK, 0 },
	};

	for (unsigned i = 0; i < sizeof gStock.raw; i++)
		gStock.raw[i] = 0;
	for (unsigned i = 0; i < SETTINGS_EXT_LEN; i++)
		gStock.ext[i] = 0;
	for (unsigned i = 0; i < sizeof D / sizeof D[0]; i++)
		gStock.raw[D[i][0]] = D[i][1];
}

bool SETTINGS_Load(void)
{
	uint8_t s[SETTINGS_SET_SLOT];

	SETTINGS_Defaults();
	gStock.have_settings = gStock.have_vfo = false;
	EXTFLASH_Read(SETTINGS_ANI, gStock.ani, sizeof gStock.ani);
	EXTFLASH_Read(SETTINGS_UP_CODE, gStock.up_code, sizeof gStock.up_code);
	EXTFLASH_Read(SETTINGS_DOWN_CODE, gStock.down_code, sizeof gStock.down_code);
	if (read_current(SETTINGS_SET_HDR, SETTINGS_SET_HDR_LEN, SETTINGS_SET_LOG, SETTINGS_SET_SLOT, SETTINGS_SET_MAX, s)
	    && SETTINGS_Crc16(s, 0x5E) == (uint16_t)(s[0x5E] | s[0x5F] << 8)) {
		for (unsigned i = 0; i < sizeof gStock.raw; i++)
			gStock.raw[i] = s[i];
		for (unsigned i = 0; i < SETTINGS_EXT_LEN; i++)
			gStock.ext[i] = s[SETTINGS_EXT_OFF + i];
		gStock.have_settings = true;
	}
	if (read_current(SETTINGS_VFO_HDR, SETTINGS_VFO_HDR_LEN, SETTINGS_VFO_LOG, SETTINGS_VFO_SLOT, SETTINGS_VFO_MAX, s)
	    && SETTINGS_Crc16(s, 0x40) == (uint16_t)(s[0x40] | s[0x41] << 8)) {
		for (unsigned i = 0; i < sizeof gStock.vfo_raw; i++)
			gStock.vfo_raw[i] = s[i];
		gStock.have_vfo = SETTINGS_DecodeVfo(s, &gStock.vfo[0]) && SETTINGS_DecodeVfo(s + 32, &gStock.vfo[1]);
	}
	return gStock.have_settings || gStock.have_vfo;
}

/* ------------------------------------------------------------------------------------------- byte 18 */
/* Byte 18 of a VFO record: in one radio's whole log it is 4 on every airband record and 0 on all others (145, 160,
 * 435 MHz), so not the stock band index (that would be 1 at 435 MHz). Its meaning is not traced. Kept as stock wrote
 * it while the frequency is unchanged; else 4 on airband, 0 elsewhere (unconfirmed for 200-400 MHz). */
static uint8_t byte18_for(const uint8_t rec[32], uint32_t f)
{
	StockVfo_t old;

	if (SETTINGS_DecodeVfo(rec, &old) && old.freq == f)
		return rec[18];
	return f >= 10800000u && f < 13600000u ? 4 : 0;
}

static void put_digits(uint8_t *p, int n, uint32_t v)
{
	for (int i = n - 1; i >= 0; i--) {
		p[i] = (uint8_t)(v % 10u);
		v /= 10u;
	}
}

/* Only the fields we model change; every other byte of the record stays as stock wrote it. */
void SETTINGS_EncodeVfo(uint8_t rec[32], const StockVfo_t *v)
{
	rec[18] = byte18_for(rec, v->freq);
	put_digits(rec, 8, v->freq);
	rec[8] = (uint8_t)v->rx_tone;
	rec[9] = (uint8_t)(v->rx_tone >> 8);
	rec[10] = (uint8_t)v->tx_tone;
	rec[11] = (uint8_t)(v->tx_tone >> 8);
	rec[14] = (uint8_t)((rec[14] & ~0x60u) | (v->shift > 0 ? 0x20u : v->shift < 0 ? 0x40u : 0u));
	rec[16] = v->power ? 1 : 0;
	rec[17] = (uint8_t)((rec[17] & ~0x40u) | (v->narrow ? 0x40u : 0u));
	rec[19] = v->step;
	put_digits(rec + 20, 7, v->offset / 10u);
}

#if SETTINGS_ENABLE_SAVE
/* ------------------------------------------------------------------------------------------- saving
 * As stock (settings sub_01E40620, VFOs at 0x01E40836): write the next slot after the current one, then clear the
 * current slot's header bit (table 0x01E5C06D: FE FC F8 F0 E0 C0 80 00, one byte of the header); past the last slot
 * erase the 4 KB sector and write slot 0. A slot the same as the current one is not written again. */
static int log_append(uint32_t header, uint32_t header_len, uint32_t first, uint32_t size, unsigned max,
                      const uint8_t *slot)
{
	static const uint8_t CLEAR[8] = { 0xFE, 0xFC, 0xF8, 0xF0, 0xE0, 0xC0, 0x80, 0x00 };
	unsigned i = current_index(header, header_len);
	uint8_t cur[SETTINGS_SET_SLOT];
	uint32_t k;

	if (i <= max) {
		EXTFLASH_Read(first + i * size, cur, size);
		for (k = 0; k < size && cur[k] == slot[k]; k++)
			;
		if (k == size)
			return 0;
		if (EXTFLASH_Program(first + (i + 1u) * size, slot, size))
			return -1;
		return EXTFLASH_Program(header + (i >> 3), &CLEAR[i & 7u], 1);
	}
	if (EXTFLASH_Erase4K(header & ~0xFFFu))
		return -1;
	return EXTFLASH_Program(first, slot, size);
}

int SETTINGS_SaveSettings(void)
{
	uint8_t s[SETTINGS_SET_SLOT];
	uint16_t c;

	for (unsigned i = 0; i < SETTINGS_SET_SLOT; i++)
		s[i] = i < sizeof gStock.raw ? gStock.raw[i] : 0;
	for (unsigned i = 0; i < SETTINGS_EXT_LEN; i++)
		s[SETTINGS_EXT_OFF + i] = gStock.ext[i];
	c = SETTINGS_Crc16(s, 0x5E);
	s[0x5E] = (uint8_t)c;
	s[0x5F] = (uint8_t)(c >> 8);
	return log_append(SETTINGS_SET_HDR, SETTINGS_SET_HDR_LEN, SETTINGS_SET_LOG, SETTINGS_SET_SLOT, SETTINGS_SET_MAX, s);
}

static int save_vfo_slot(uint8_t s[SETTINGS_VFO_SLOT])
{
	uint16_t c = SETTINGS_Crc16(s, 0x40);

	s[0x40] = (uint8_t)c;
	s[0x41] = (uint8_t)(c >> 8);
	return log_append(SETTINGS_VFO_HDR, SETTINGS_VFO_HDR_LEN, SETTINGS_VFO_LOG, SETTINGS_VFO_SLOT, SETTINGS_VFO_MAX, s);
}

int SETTINGS_SaveVfos(void)
{
	uint8_t s[SETTINGS_VFO_SLOT];

	for (unsigned i = 0; i < 64; i++)
		s[i] = gStock.vfo_raw[i];
	SETTINGS_EncodeVfo(s, &gStock.vfo[0]);
	SETTINGS_EncodeVfo(s + 32, &gStock.vfo[1]);
	return save_vfo_slot(s);
}

int SETTINGS_SaveVfoRaw(const uint8_t rec[64])
{
	uint8_t s[SETTINGS_VFO_SLOT];

	for (unsigned i = 0; i < 64; i++)
		s[i] = gStock.vfo_raw[i] = rec[i];
	gStock.have_vfo = SETTINGS_DecodeVfo(rec, &gStock.vfo[0]) && SETTINGS_DecodeVfo(rec + 32, &gStock.vfo[1]);
	return save_vfo_slot(s);
}
#endif
