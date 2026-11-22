/* Open-UV5R-Mini: VOX, the stock way.
 *
 * Stock (0x01E50060): the mic level is ADC channel 0 (PA1), 10-bit >> 2. It is compared with a threshold from the
 * table 21, 31, ... 111 (0x01E65721) by the VOX level (setting 0x02); while transmitting the threshold is 8 lower.
 * Above it, transmit starts or is held for (VOX delay + 5) x 100 ms (setting 0x20: 0.5 + 0.1 n s). VOX is on with
 * setting 0x3A. Transmit itself is still guarded by radio/radio.c (RADIO_ENABLE_TX, bands, calibration).
 */
#ifndef APP_VOX_H
#define APP_VOX_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { VOX_NONE = 0, VOX_START, VOX_STOP } VoxAction_t;

void        VOX_Reset(void);
/* Every tick: mic = the 8-bit level, transmitting = whether the radio is in TX now. */
VoxAction_t VOX_Tick(uint8_t mic, uint8_t level, uint8_t delay, bool transmitting, uint16_t tick_ms);

#endif
