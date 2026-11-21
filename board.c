/* Open-UV5R-Mini: board bring-up. */
#include "board.h"
#include "driver/gpio.h"
#include "driver/system.h"

void BOARD_Init(void)
{
	/* Same order as tests/radio/blink2.c, the start that worked on a radio (2026-11-21): power latch and backlight
	 * on, a pause, then the stock app's first ROM call, P33 register 0x80 (watchdog control) to 0
	 * (sub_01E46924). Without that call the watchdog clear p33_or(0x80, 0x40) was followed by restarts; called
	 * straight after reset, before the latch and the pause, the radio hardly came on. */
	GPIO_ConfigOutput(PIN_POWER_HOLD, true);
	GPIO_ConfigOutput(PIN_LCD_BACKLIGHT, true);
	for (volatile uint32_t i = 1000000; i; i--)
		;
	P33_Write(0x80, 0);
	GPIO_ConfigInput(PIN_POWER_SENSE, true);
}

/* Same as the stock sub_01E4C978 after its debounce: drop the latch and wait for power to go. */
void BOARD_PowerOff(void)
{
	GPIO_ClearBit(PIN_POWER_HOLD);
	SYSTEM_DelayMs(250);
	for (;;)
		;
}
