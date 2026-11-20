/* Open-UV5R-Mini: delays. */
#include "driver/system.h"

#define LOOPS_PER_US 32u /* about 3 cycles per loop at 96 MHz */

void SYSTEM_DelayUs(uint32_t us)
{
	for (volatile uint32_t i = us * LOOPS_PER_US; i; i--)
		;
}

void SYSTEM_DelayMs(uint32_t ms)
{
	while (ms--)
		SYSTEM_DelayUs(1000);
}
