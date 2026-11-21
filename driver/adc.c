/* Open-UV5R-Mini: SAR ADC, polled. See driver/adc.h. */
#include "driver/adc.h"
#include "bsp/ac695n.h"
#include "driver/gpio.h"

#define ADC_CON REG32(0x001E3100u)
#define ADC_RES REG32(0x001E3104u)

static void analog_pin(uint8_t pin)
{
	GPIO_Port_t *p = GPIO_PIN_TO_PORT(pin);
	uint32_t b = GPIO_PIN_TO_BIT(pin);

	REG32((uintptr_t)p + 0x20u) &= ~b;   /* as stock: the register after PD, bit cleared too */
	p->DIE &= ~b;
	p->DIR |= b;
	p->PD &= ~b;
	p->PU &= ~b;
}

void ADC_Init(void)
{
	analog_pin(IO_PORTA(1));
	analog_pin(IO_PORTB(3));
}

static int wait_ready(void)
{
	for (uint32_t i = 0; i < 100000u; i++)
		if (ADC_CON & 0x80u)
			return 1;
	return 0;
}

uint16_t ADC_Read(uint8_t channel, uint8_t n)
{
	uint32_t sum = 0;

	if (!n)
		n = 1;
	ADC_CON = 0xF04Eu | (uint32_t)(channel & 15u) << 8;   /* stock 0xF06E without bit 5 */
	ADC_CON |= 0x10u;
	ADC_CON |= 0x40u;
	if (!wait_ready())                                       /* the first result after a switch is thrown away */
		return 0xFFFF;
	(void)ADC_RES;
	ADC_CON |= 0x40u;
	for (uint8_t i = 0; i < n; i++) {
		if (!wait_ready())
			return 0xFFFF;
		sum += ADC_RES & 0x3FFu;
		ADC_CON |= 0x40u;
	}
	return (uint16_t)(sum / n);
}
