/* Open-UV5R-Mini: delays. */
#ifndef DRIVER_SYSTEM_H
#define DRIVER_SYSTEM_H

#include <stdint.h>

/* Busy-wait delays. Calibrated for a core clock of at most 96 MHz (the bootloader's PLL setting is not
 * known yet), so on a slower clock they run long, never short. Replace with a timer once one is mapped. */
void SYSTEM_DelayUs(uint32_t us);
void SYSTEM_DelayMs(uint32_t ms);

#endif
