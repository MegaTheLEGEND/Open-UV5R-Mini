/* Open-UV5R-Mini: JieLi AC695N register definitions.
 *
 * Every address here was read out of the stock UV-5R Mini firmware (see docs/RE-NOTES.md);
 * nothing comes from JieLi's SDK.
 */
#ifndef BSP_AC695N_H
#define BSP_AC695N_H

#include <stdint.h>

#define REG32(a) (*(volatile uint32_t *)(a))

/* GPIO ports: one 0x40-byte block per port (table at 0x01E00664 in the stock image). */
#define GPIO_PORTA_BASE 0x001E5000u
#define GPIO_PORTB_BASE 0x001E5040u
#define GPIO_PORTC_BASE 0x001E5080u
#define GPIO_PORTD_BASE 0x001E50C0u

typedef struct {
	volatile uint32_t OUT; /* +0x00 output level            */
	volatile uint32_t IN;  /* +0x04 input level             */
	volatile uint32_t DIR; /* +0x08 1 = input, 0 = output   */
	volatile uint32_t DIE; /* +0x0C digital input enable    */
	volatile uint32_t PU;  /* +0x10 pull-up                 */
	volatile uint32_t PD;  /* +0x14 pull-down               */
} GPIO_Port_t;

#define GPIOA ((GPIO_Port_t *)(uintptr_t)GPIO_PORTA_BASE)
#define GPIOB ((GPIO_Port_t *)(uintptr_t)GPIO_PORTB_BASE)
#define GPIOC ((GPIO_Port_t *)(uintptr_t)GPIO_PORTC_BASE)
#define GPIOD ((GPIO_Port_t *)(uintptr_t)GPIO_PORTD_BASE)

/* JieLi pin numbers: PA0 = 0, PB0 = 16, PC0 = 32, PD0 = 48. */
#define IO_PORTA(n) (0 + (n))
#define IO_PORTB(n) (16 + (n))
#define IO_PORTC(n) (32 + (n))
#define IO_PORTD(n) (48 + (n))

/* SPI1 (LCD in the stock firmware; SPI0/1/2 = 0x1E1C00/0x1E1D00/0x1E1E00): +0 CON, +4 BAUD, +0xC DMA address, +0x10 DMA count.
 * Not used yet: the first port bit-bangs the LCD. */
#define SPI1_BASE 0x001E1D00u

/* Power-domain ("P33") registers, reached through a small serial interface at 0x1E3E08 (+0 CON, +4 DAT).
 *
 * The stock firmware's P33 routines (0x02000058 read, 0x020000A0 write, 0x02000156 or) are not in a mask ROM:
 * 0x02000000 runs the code the stock app copies into RAM at start-up (flash 0x01E6AAAC -> RAM 0, 53492 bytes).
 * Calling them from our app ran into whatever was in RAM and restarted the chip (on a radio, 2026-11-21). This is
 * our own version of what they do:
 *   CON bit 0 selects the chip, bit 8 the second register bank (addresses with bit 15 set, e.g. 0x8099);
 *   one byte: DAT = byte, CON |= 0x10, wait while CON bit 1, the answer is in DAT;
 *   read:  ((addr >> 8) & 3) | 0x80, addr & 0xFF, then a dummy 0x5E whose answer is the value;
 *   write: (addr >> 8) & 3, addr & 0xFF, value.   (or: first byte | 0x20; and: | 0x40)
 * The stock routines also mask interrupts around this; our app runs with interrupts off. */
#define P33_CON REG32(0x001E3E08u)
#define P33_DAT REG32(0x001E3E0Cu)

static inline uint8_t P33_Xfer(uint8_t b)
{
	P33_DAT = b;
	P33_CON |= 0x10u;
	while (P33_CON & 0x2u)
		;
	return (uint8_t)P33_DAT;
}

static inline void P33_Begin(uint16_t reg)
{
	if (reg & 0x8000u)
		P33_CON |= 0x101u;
	else {
		P33_CON &= ~0x100u;
		P33_CON |= 0x1u;
	}
}

static inline void P33_End(void) { P33_CON &= ~0x101u; }

static inline uint8_t P33_Read(uint16_t reg)
{
	P33_Begin(reg);
	P33_Xfer((uint8_t)(((reg >> 8) & 3u) | 0x80u));
	P33_Xfer((uint8_t)reg);
	uint8_t v = P33_Xfer(0x5E);
	P33_End();
	return v;
}

static inline void P33_Or(uint16_t reg, uint8_t mask)
{
	P33_Begin(reg);
	P33_Xfer((uint8_t)(((reg >> 8) & 3u) | 0x20u));
	P33_Xfer((uint8_t)reg);
	P33_Xfer(mask);
	P33_End();
}

static inline void P33_Write(uint16_t reg, uint8_t val)
{
	P33_Begin(reg);
	P33_Xfer((uint8_t)((reg >> 8) & 3u));
	P33_Xfer((uint8_t)reg);
	P33_Xfer(val);
	P33_End();
}

/* PR port (power-domain GPIO) as used for the LCD D/C line. */
#define P33_PR_OUT 0x8099u
#define P33_PR_DIR 0x809Au /* bit clear = output */

#endif
