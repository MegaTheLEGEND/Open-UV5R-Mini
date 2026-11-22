/* Open-UV5R-Mini: delays and the watchdog. */
#ifndef DRIVER_SYSTEM_H
#define DRIVER_SYSTEM_H

#include <stdint.h>

/* Busy-wait delays. Calibrated for a core clock of at most 96 MHz (the bootloader's PLL setting is not
 * known yet), so on a slower clock they run long, never short. Replace with a timer once one is mapped.
 * SYSTEM_DelayMs clears the watchdog every millisecond, so every wait keeps it fed. */
void SYSTEM_DelayUs(uint32_t us);
void SYSTEM_DelayMs(uint32_t ms);

/* Clear the watchdog as the stock app does: p33_or(0x80, 0x40) through the mask ROM (docs/EMULATOR.md).
 * Harmless if the watchdog isn't running. */
void SYSTEM_WatchdogClear(void);

#endif
