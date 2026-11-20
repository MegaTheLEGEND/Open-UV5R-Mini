/* Open-UV5R-Mini: GPIO helpers. Register layout from the stock board init (sub_01E485EC). */
#include "driver/gpio.h"

void GPIO_ConfigOutput(uint8_t pin, bool initial)
{
	GPIO_Port_t *p = GPIO_PIN_TO_PORT(pin);
	uint32_t b = GPIO_PIN_TO_BIT(pin);

	p->PD &= ~b;
	p->PU &= ~b;
	p->DIE |= b;
	GPIO_Write(pin, initial);
	p->DIR &= ~b;
}

void GPIO_ConfigInput(uint8_t pin, bool pull_up)
{
	GPIO_Port_t *p = GPIO_PIN_TO_PORT(pin);
	uint32_t b = GPIO_PIN_TO_BIT(pin);

	p->PD &= ~b;
	if (pull_up)
		p->PU |= b;
	else
		p->PU &= ~b;
	p->DIE |= b;
	p->DIR |= b;
}

void GPIO_SetInput(uint8_t pin)  { GPIO_PIN_TO_PORT(pin)->DIR |= GPIO_PIN_TO_BIT(pin); }
void GPIO_SetOutput(uint8_t pin) { GPIO_PIN_TO_PORT(pin)->DIR &= ~GPIO_PIN_TO_BIT(pin); }
