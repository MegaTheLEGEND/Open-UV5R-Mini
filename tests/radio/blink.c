/* Open-UV5R-Mini: first-boot test for a real radio. Uses only the LCD backlight (PB7), no screen driver.
 *
 *  1. Backlight on, held for one million loop turns: proves our code runs at all.
 *  2. Five slow blinks (on/off every million loop turns), no mask-ROM calls: the blink speed gives the CPU speed.
 *  3. Then fast blinks for ever, clearing the watchdog through the mask ROM (p33_or(0x80, 0x40)) each time:
 *     if the blinking stops here, the ROM call is the problem; if it starts again from step 1 now and then, the
 *     chip is resetting (watchdog).
 *
 * Linked with start.S and firmware.ld like the real app. Build: see tests/radio/README.md.
 */
#include <stdint.h>
#include "bsp/ac695n.h"

#define POWER_HOLD (1u << 10) /* PB10 */
#define BACKLIGHT  (1u << 7)  /* PB7 */
#define TURNS      1000000u

static void out_high(GPIO_Port_t *p, uint32_t b)
{
	p->PD &= ~b;
	p->PU &= ~b;
	p->DIE |= b;
	p->OUT |= b;
	p->DIR &= ~b;
}

static void spin(uint32_t n)
{
	for (volatile uint32_t i = n; i; i--)
		;
}

static void wdt_clear(void)
{
	((void (*)(uint16_t, uint8_t))(uintptr_t)0x02000156u)(0x80, 0x40);
}

int main(void)
{
	out_high(GPIOB, POWER_HOLD);
	out_high(GPIOB, BACKLIGHT);
	spin(TURNS);

	for (int i = 0; i < 5; i++) {
		GPIOB->OUT &= ~BACKLIGHT;
		spin(TURNS);
		GPIOB->OUT |= BACKLIGHT;
		spin(TURNS);
	}
	for (;;) {
		GPIOB->OUT ^= BACKLIGHT;
		wdt_clear();
		spin(TURNS / 4);
	}
}
