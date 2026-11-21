/* Open-UV5R-Mini: SAR ADC, polled.
 *
 * From the stock app (sub_01E3CD74 and its callers): the ADC is at 0x1E3100, +0 CON, +4 RES. A conversion is
 * set up with CON = 0xF06E | channel << 8, then bit 4 and bit 6 are set; bit 7 of CON says the result is ready,
 * and setting bit 6 again starts the next one. The stock app sums ten results where it reads directly. We leave
 * bit 5 (taken to be the interrupt enable, the stock scan list is filled from an interrupt) off and poll.
 *
 * Board (stock sub_01E4698A): PA1 = channel 0, PB3 = channel 6. PB3 is the battery: stock sub_01E46A30 reads
 * channel 6 for the battery level and the voltage screen. The pins are made analog the stock way: digital input
 * and pulls off, direction input.
 *
 * Not tried on a radio yet.
 */
#ifndef DRIVER_ADC_H
#define DRIVER_ADC_H

#include <stdint.h>

#define ADC_CH_PA1 0u
#define ADC_CH_BATTERY 6u     /* PB3 */

void     ADC_Init(void);
/* One channel, n results averaged; 10-bit (0..1023). 0xFFFF if the ADC never says it's ready. */
uint16_t ADC_Read(uint8_t channel, uint8_t n);

#endif
