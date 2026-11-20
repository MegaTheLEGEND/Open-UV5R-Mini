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

/* SPI2 (LCD in the stock firmware): +0 CON, +4 BAUD, +0xC DMA address, +0x10 DMA count.
 * Not used yet: the first port bit-bangs the LCD. */
#define SPI2_BASE 0x001E1D00u

/* Power-domain ("P33") registers are reached through routines in the chip's mask ROM.
 * The stock firmware calls them at these fixed addresses. */
#define ROM_P33_READ  0x02000058u /* uint8_t  f(uint16_t reg)              */
#define ROM_P33_WRITE 0x020000A0u /* void     f(uint16_t reg, uint8_t val) */

static inline uint8_t P33_Read(uint16_t reg)
{
	return ((uint8_t (*)(uint16_t))(uintptr_t)ROM_P33_READ)(reg);
}

static inline void P33_Write(uint16_t reg, uint8_t val)
{
	((void (*)(uint16_t, uint8_t))(uintptr_t)ROM_P33_WRITE)(reg, val);
}

/* PR port (power-domain GPIO) as used for the LCD D/C line. */
#define P33_PR_OUT 0x8099u
#define P33_PR_DIR 0x809Au /* bit clear = output */

#endif
