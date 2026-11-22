/* Open-UV5R-Mini: battery voltage and level, the stock way.
 *
 * Stock sub_01E46A30: the battery value is the ADC result of channel 6 (PB3) with its two low bits dropped (8 bits);
 * the voltage screen shows value * 435 / 10000 volts (" %d.%dV"). The level compares the value with the radio's six
 * calibrated thresholds at external flash 0xF200 (Codeplug Studio calibration.md; 0xFF = blank). The first threshold
 * counts as passed at >=, the others at > (stock sub_01E46D1C).
 */
#ifndef APP_BATTERY_H
#define APP_BATTERY_H

#include <stdbool.h>
#include <stdint.h>

void    BATTERY_Set(uint16_t adc10);       /* a 10-bit ADC result of channel 6 (0xFFFF: no reading) */
uint8_t BATTERY_Raw(void);                 /* the stock 8-bit value, 0 if none */
uint8_t BATTERY_Tenths(void);              /* volts * 10 */
uint8_t BATTERY_Level(void);               /* 0..6 thresholds passed; 0xFF without calibration */
/* Below the second threshold: stock makes its battery icon blink there (levels 0 and 1; stock codes 4 and 5,
 * 0x01E4F0B6). */
bool    BATTERY_Low(void);

#endif
