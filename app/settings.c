/* Open-UV5R-Mini: the stock settings and VFOs, read only. See app/settings.h. */
#include "app/settings.h"
#include "app/tones.h"
#ifndef CHANNELS_HOST_TEST
#include "driver/extflash.h"
#else
void EXTFLASH_Read(uint32_t addr, uint8_t *buf, uint32_t len);
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

static bool all_ff(const uint8_t *p, uint32_t n)
{
	while (n--)
		if (*p++ != 0xFF)
			return false;
	return true;
}

/* The last written slot of a log: the one before the first all-0xFF slot (or the last that fits). */
static bool last_slot(uint32_t base, uint32_t end, uint32_t size, uint8_t *buf)
{
	uint32_t a, last = 0;

	for (a = base; a + size <= end; a += size) {
		EXTFLASH_Read(a, buf, size);
		if (all_ff(buf, size))
			break;
		last = a;
	}
	if (!last)
		return false;
	EXTFLASH_Read(last, buf, size);
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
	for (unsigned i = 0; i < sizeof D / sizeof D[0]; i++)
		gStock.raw[D[i][0]] = D[i][1];
}

bool SETTINGS_Load(void)
{
	uint8_t s[SETTINGS_SET_SLOT];

	SETTINGS_Defaults();
	gStock.have_settings = gStock.have_vfo = false;
	if (last_slot(SETTINGS_SET_LOG, SETTINGS_SET_END, SETTINGS_SET_SLOT, s)
	    && SETTINGS_Crc16(s, 0x5E) == (uint16_t)(s[0x5E] | s[0x5F] << 8)) {
		for (unsigned i = 0; i < sizeof gStock.raw; i++)
			gStock.raw[i] = s[i];
		gStock.have_settings = true;
	}
	if (last_slot(SETTINGS_VFO_LOG, SETTINGS_VFO_END, SETTINGS_VFO_SLOT, s)
	    && SETTINGS_Crc16(s, 0x40) == (uint16_t)(s[0x40] | s[0x41] << 8))
		gStock.have_vfo = SETTINGS_DecodeVfo(s, &gStock.vfo[0]) && SETTINGS_DecodeVfo(s + 32, &gStock.vfo[1]);
	return gStock.have_settings || gStock.have_vfo;
}
