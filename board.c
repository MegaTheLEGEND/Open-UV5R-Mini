/* Open-UV5R-Mini: board bring-up. */
#include "board.h"
#include "driver/gpio.h"
#include "driver/system.h"

void BOARD_Init(void)
{
	/* The stock app's very first act (sub_01E46924): P33 register 0x80 (watchdog control) to 0. Without it, the
	 * watchdog clear p33_or(0x80, 0x40) was followed by restarts on a radio; with it, it wasn't
	 * (tests/radio/blink.c vs blink2.c, 2026-11-21). */
	P33_Write(0x80, 0);

	/* Keep the power latch on first, as the stock init drives PB10 high. */
	GPIO_ConfigOutput(PIN_POWER_HOLD, true);
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
