/* Open-UV5R-Mini: battery voltage and level. See app/battery.h. */
#include "app/battery.h"
#include "store/calib.h"

static uint8_t s_raw;
static int32_t s_acc;            /* the value in 1/16 steps, smoothed */

void BATTERY_Set(uint16_t adc10)
{
	int32_t v = adc10 == 0xFFFFu ? 0 : (int32_t)(adc10 >> 2) * 16;

	/* a little smoothing: the voltage sags while transmitting and with the speaker */
	s_acc = s_raw ? s_acc + (v - s_acc) / 4 : v;
	s_raw = (uint8_t)((s_acc + 8) / 16);
}

uint8_t BATTERY_Raw(void) { return s_raw; }

uint8_t BATTERY_Tenths(void)
{
	return (uint8_t)((uint32_t)s_raw * 435u / 1000u);
}

uint8_t BATTERY_Level(void)
{
	uint8_t n = 0;

	if (!CALIB_Present() || CALIB_Byte(0x200) == 0xFF)
		return 0xFF;
	for (uint16_t i = 0; i < 6; i++) {
		uint8_t t = CALIB_Byte((uint16_t)(0x200 + i));
		if (t != 0xFF && (i == 0 ? s_raw >= t : s_raw > t))     /* as stock sub_01E46D1C */
			n++;
	}
	return n;
}

bool BATTERY_Low(void)
{
	return BATTERY_Raw() && BATTERY_Level() <= 1;
}

/* Millivolts from the smoothed value: 43.5 mV a step (stock 435 / 10000 V), s_acc in 1/16 steps. */
uint16_t BATTERY_Millivolts(void)
{
	return s_raw ? (uint16_t)((uint32_t)s_acc * 435u / 160u) : 0;
}

/* Percent. With the radio's calibration: from its battery thresholds, so it agrees with the bars. The first
 * threshold is 0 % and the fifth 100 % (stock counts 5 passed as full, 4 bars; the sixth, 8.9 V on Sean's radio, is
 * only reached on the charger), 25 % between each, straight lines in 1/16 steps. On a radio (2026-11-24) the
 * thresholds were 6.09 / 6.31 / 6.74 / 7.18 / 7.96 / 8.92 V: a two-cell pack, so a one-cell table showed 100 % at
 * 6.2 V. Without calibration: a typical Li-ion curve per cell, two cells above 5 V. */
uint8_t BATTERY_Percent(void)
{
	static const uint16_t MV[] = { 4150, 4050, 3950, 3850, 3780, 3720, 3680, 3600, 3450, 3300 };
	static const uint8_t  PC[] = {  100,   90,   80,   65,   50,   35,   20,   10,    3,    0 };
	uint16_t mv = BATTERY_Millivolts();

	if (!s_raw)
		return 0;
	if (BATTERY_Level() != 0xFF) {
		int32_t t[5];
		for (uint16_t i = 0; i < 5; i++)
			t[i] = (int32_t)CALIB_Byte((uint16_t)(0x200 + i)) * 16;
		if (s_acc <= t[0])
			return 0;
		for (unsigned i = 1; i < 5; i++)
			if (t[i] > t[i - 1] && s_acc < t[i])
				return (uint8_t)(25 * (i - 1) + (s_acc - t[i - 1]) * 25 / (t[i] - t[i - 1]));
		return 100;
	}
	if (mv > 5000u)
		mv /= 2u;                        /* two cells */
	if (mv >= MV[0])
		return 100;
	for (unsigned i = 1; i < sizeof MV / sizeof MV[0]; i++)
		if (mv >= MV[i])
			return (uint8_t)(PC[i] + (uint32_t)(mv - MV[i]) * (PC[i - 1] - PC[i]) / (MV[i - 1] - MV[i]));
	return 0;
}
