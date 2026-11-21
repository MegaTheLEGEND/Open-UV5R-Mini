/* Open-UV5R-Mini: board bring-up. */
#include "board.h"
#include "driver/gpio.h"
#include "driver/system.h"

void BOARD_Init(void)
{
	/* Power latch and backlight first. The watchdog register (P33 0x80) is left alone: every build that wrote it
	 * on a radio (2026-11-21) cut out after a few seconds, and the blink test that never touched it ran on. */
	GPIO_ConfigOutput(PIN_POWER_HOLD, true);
	GPIO_ConfigOutput(PIN_LCD_BACKLIGHT, true);
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
