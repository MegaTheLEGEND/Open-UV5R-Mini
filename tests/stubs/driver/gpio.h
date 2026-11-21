/* PC-test stand-in for driver/gpio.h: pins are an array. */
#ifndef DRIVER_GPIO_H
#define DRIVER_GPIO_H
#include <stdbool.h>
#include <stdint.h>
extern bool test_pins[128];
static inline void GPIO_SetBit(uint8_t pin)   { test_pins[pin] = true; }
static inline void GPIO_ClearBit(uint8_t pin) { test_pins[pin] = false; }
static inline bool GPIO_CheckBit(uint8_t pin) { return test_pins[pin]; }
static inline void GPIO_Write(uint8_t pin, bool on) { test_pins[pin] = on; }
static inline void GPIO_ConfigOutput(uint8_t pin, bool initial) { test_pins[pin] = initial; }
static inline void GPIO_ConfigInput(uint8_t pin, bool pull_up) { (void)pin; (void)pull_up; }
#endif
