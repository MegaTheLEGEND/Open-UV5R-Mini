/* Open-UV5R-Mini: battery voltage and level. See app/battery.h. */
#include "app/battery.h"
#include "app/calib.h"

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
