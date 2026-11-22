/* Open-UV5R-Mini: the radio's own calibration. See app/calib.h. */
#include "app/calib.h"

static uint8_t s_cal[CALIB_LEN];
static bool s_present;

void CALIB_Set(const uint8_t *data)
{
	for (uint32_t i = 0; i < CALIB_LEN; i++)
		s_cal[i] = data[i];
	s_present = s_cal[0] != 0xFF;
}

bool CALIB_Present(void) { return s_present; }

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

uint8_t CALIB_PaBias(uint32_t freq_10hz, uint8_t power)
{
	static const uint16_t FRAME[3] = { 0x000, 0x080, 0x040 };
	int mhz = (int)(freq_10hz / 100000u), row, i;

	if (!s_present || power > 2)
		return 0;
	if (mhz >= 400) {
		row = 0x00;
		i = clampi((mhz - 400) / 10, 0, 12);
	} else if (mhz >= 300) {
		row = 0x30;
		i = clampi((mhz - 350) / 5, 0, 15);
	} else if (mhz >= 200) {
		row = 0x20;
		i = clampi((mhz - 200) / 5, 0, 12);
	} else {
		row = 0x10;
		i = clampi((mhz - 130) / 3, 0, 15);
	}
	return s_cal[FRAME[power] + row + i];
}

uint8_t CALIB_Byte(uint16_t offset)
{
	return offset < CALIB_LEN ? s_cal[offset] : 0xFF;
}

/* Sub-audio levels (REG_51 low 7 bits). Not calibrated: the BK4819 notes' usual values. */
uint8_t CALIB_CtcssLevel(void)
{
	return s_present && s_cal[0x211] < 0x80u ? s_cal[0x211] : 0x4A;
}

/* Not calibrated: 0x12, the low bits of stock's REG_7D init value 0xE952. */
uint8_t CALIB_MicGain(void)
{
	return s_present && s_cal[0x210] != 0xFFu ? (uint8_t)(s_cal[0x210] & 0x1Fu) : 0x12;
}

uint8_t CALIB_DcsLevel(void)
{
	return s_present && s_cal[0x212] != 0xFFu ? (uint8_t)(s_cal[0x212] & 0x7Fu) : 0x33;
}

uint8_t CALIB_PaGain(uint8_t power)
{
	return power == POWER_LOW ? 0xD7 : 0xFF;
}

void CALIB_Squelch(uint32_t freq_10hz, uint8_t level, uint8_t *open, uint8_t *close)
{
	int mhz = (int)(freq_10hz / 100000u), off, o, c;

	if (level > 9)
		level = 9;
	if (mhz >= 400)
		off = s_cal[0xE0 + clampi((mhz - 400) / 10, 0, 12)];
	else if (mhz >= 300)
		off = s_cal[0x110 + clampi((mhz - 350) / 5, 0, 9)];
	else if (mhz >= 200)
		off = s_cal[0x100 + clampi((mhz - 200) / 5, 0, 12)];
	else
		off = s_cal[0xF0 + clampi((mhz - 130) / 5, 0, 9)];
	if (mhz < 130 || (mhz >= 180 && mhz < 200)) {
		o = s_cal[0xC9] - off;
		c = o - 3;
	} else {
		o = s_cal[0xC0 + level] - off;
		c = s_cal[0xD0 + level] - off;
	}
	*open = (uint8_t)clampi(o, 0, 255);
	*close = (uint8_t)clampi(c, 0, 255);
}
