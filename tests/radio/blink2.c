/* Open-UV5R-Mini: second first-boot test (2026-11-21). The first one (blink.c) showed our code runs, and that the
 * first mask-ROM call restarts the chip. This one starts the way the stock app does (start_like_stock.S) and makes
 * the stock app's own first ROM call, p33_tx(0x80, 0) (stock sub_01E46924), before anything else.
 *
 *  1. Backlight on for one million loop turns: our code runs.
 *  2. p33_tx(0x80, 0) through the ROM. Then 3 slow blinks: the ROM call came back.
 *  3. Fast blinks for ever, each with p33_or(0x80, 0x40) (the stock watchdog clear) and a p33_rx/p33_tx of PR_OUT
 *     (what the LCD driver needs): if this keeps blinking, ROM calls work with this start-up.
 * A restart shows as the light staying on for a moment, then slow blinks again.
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

static void rom_p33_tx(uint16_t reg, uint8_t v) { ((void (*)(uint16_t, uint8_t))(uintptr_t)0x020000A0u)(reg, v); }
static uint8_t rom_p33_rx(uint16_t reg) { return ((uint8_t (*)(uint16_t))(uintptr_t)0x02000058u)(reg); }
static void rom_p33_or(uint16_t reg, uint8_t m) { ((void (*)(uint16_t, uint8_t))(uintptr_t)0x02000156u)(reg, m); }

int main(void)
{
	out_high(GPIOB, POWER_HOLD);
	out_high(GPIOB, BACKLIGHT);
	spin(TURNS);

	rom_p33_tx(0x80, 0);
	for (int i = 0; i < 3; i++) {
		GPIOB->OUT &= ~BACKLIGHT;
		spin(TURNS);
		GPIOB->OUT |= BACKLIGHT;
		spin(TURNS);
	}
	for (;;) {
		GPIOB->OUT ^= BACKLIGHT;
		rom_p33_or(0x80, 0x40);
		rom_p33_tx(P33_PR_OUT, rom_p33_rx(P33_PR_OUT));
		spin(TURNS / 4);
	}
}
