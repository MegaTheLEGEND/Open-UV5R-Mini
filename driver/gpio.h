/* Open-UV5R-Mini: GPIO helpers (egzumer-style). */
#ifndef DRIVER_GPIO_H
#define DRIVER_GPIO_H

#include <stdbool.h>
#include <stdint.h>
#include "bsp/ac695n.h"

#define GPIO_PIN_TO_PORT(pin) ((GPIO_Port_t *)(uintptr_t)(GPIO_PORTA_BASE + ((pin) >> 4) * 0x40u))
#define GPIO_PIN_TO_BIT(pin)  (1u << ((pin) & 15u))

static inline void GPIO_SetBit(uint8_t pin)    { GPIO_PIN_TO_PORT(pin)->OUT |= GPIO_PIN_TO_BIT(pin); }
static inline void GPIO_ClearBit(uint8_t pin)  { GPIO_PIN_TO_PORT(pin)->OUT &= ~GPIO_PIN_TO_BIT(pin); }
static inline bool GPIO_CheckBit(uint8_t pin)  { return (GPIO_PIN_TO_PORT(pin)->IN & GPIO_PIN_TO_BIT(pin)) != 0; }
static inline void GPIO_Write(uint8_t pin, bool on) { if (on) GPIO_SetBit(pin); else GPIO_ClearBit(pin); }

/* Configure a pin the way the stock board init does: digital, no pulls unless asked. */
void GPIO_ConfigOutput(uint8_t pin, bool initial);
void GPIO_ConfigInput(uint8_t pin, bool pull_up);
void GPIO_SetInput(uint8_t pin);  /* direction only, for bidirectional lines */
void GPIO_SetOutput(uint8_t pin);

#endif
