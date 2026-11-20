/* Open-UV5R-Mini: board bring-up. */
#include "board.h"
#include "driver/gpio.h"
#include "driver/system.h"

void BOARD_Init(void)
{
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
