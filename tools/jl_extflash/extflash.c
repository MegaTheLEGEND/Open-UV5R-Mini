/* jl_extflash: a small RAM program for the AC695N's ROM USB download mode (UBOOT1.00). It reads the radio's
 * external SPI flash (GT25Q16A, 2 MB) into RAM, where the PC reads it back with the ROM's memory-read command.
 * It only reads: it sends the flash nothing but JEDEC ID (0x9F) and READ (0x03).
 *
 * Pins (docs/RE-NOTES.md, stock board init sub_01E485EC): CS = PA3, SCK = PB0, MOSI = PB1, MISO = PB2 (SPI1
 * port A, shared with the LCD, whose CS is PB5). The SPI is bit-banged, so no SPI1 register is touched.
 *
 * The PC writes a request at PARAMS, jumps to extflash_entry, and reads the answer from PARAMS and BUF.
 * Driven by tools/jl_extflash/extflash.py. Untested until the first run on a radio.
 */
#include <stdint.h>

#define GPIOA_BASE 0x001E5000u
#define GPIOB_BASE 0x001E5040u

typedef struct {
	volatile uint32_t OUT;
	volatile uint32_t IN;
	volatile uint32_t DIR; /* 1 = input */
	volatile uint32_t DIE;
	volatile uint32_t PU;
	volatile uint32_t PD;
} port_t;

#define PA ((port_t *)GPIOA_BASE)
#define PB ((port_t *)GPIOB_BASE)

#define CS   (1u << 3) /* PA3 */
#define SCK  (1u << 0) /* PB0 */
#define MOSI (1u << 1) /* PB1 */
#define MISO (1u << 2) /* PB2 */
#define LCS  (1u << 5) /* PB5, LCD chip select: kept high */

#define CMD_ID   1u
#define CMD_READ 2u
#define CMD_COPY 3u
#define DONE     0x600DF00Du
#define MAGIC    0x45584653u /* "EXFS" */
#define BUF_MAX  0x1000u

typedef struct {
	volatile uint32_t magic;
	volatile uint32_t cmd;
	volatile uint32_t addr;
	volatile uint32_t len;
	volatile uint32_t done;
	volatile uint32_t result;
} params_t;

#define PARAMS ((params_t *)0x00012F00u)
#define BUF    ((volatile uint8_t *)0x00013000u)

static void out_pin(port_t *p, uint32_t b, int level)
{
	p->PD &= ~b;
	p->PU &= ~b;
	p->DIE |= b;
	if (level)
		p->OUT |= b;
	else
		p->OUT &= ~b;
	p->DIR &= ~b;
}

static void pins_setup(void)
{
	out_pin(PB, LCS, 1);
	out_pin(PA, CS, 1);
	out_pin(PB, SCK, 0);
	out_pin(PB, MOSI, 0);
	PB->PD &= ~MISO;
	PB->PU |= MISO;
	PB->DIE |= MISO;
	PB->DIR |= MISO;
}

/* SPI mode 0, MSB first: data is set while SCK is low, sampled on the rising edge. */
static uint8_t spi_byte(uint8_t out)
{
	uint8_t in = 0;
	for (int i = 7; i >= 0; i--) {
		if (out & (1u << i))
			PB->OUT |= MOSI;
		else
			PB->OUT &= ~MOSI;
		PB->OUT |= SCK;
		in = (uint8_t)((in << 1) | ((PB->IN & MISO) ? 1u : 0u));
		PB->OUT &= ~SCK;
	}
	return in;
}

static void cs(int level)
{
	if (level)
		PA->OUT |= CS;
	else
		PA->OUT &= ~CS;
}

__attribute__((section(".text.entry"))) void extflash_entry(void)
{
	params_t *p = PARAMS;

	p->done = 0;
	if (p->magic != MAGIC) {
		p->result = 0xBAD0u;
		p->done = DONE;
		return;
	}

	if (p->cmd != CMD_COPY)
		pins_setup();

	if (p->cmd == CMD_ID) {
		cs(0);
		spi_byte(0x9F);
		uint32_t id = (uint32_t)spi_byte(0) << 16;
		id |= (uint32_t)spi_byte(0) << 8;
		id |= spi_byte(0);
		cs(1);
		p->result = id;
	} else if (p->cmd == CMD_READ) {
		uint32_t len = p->len > BUF_MAX ? BUF_MAX : p->len;
		uint32_t addr = p->addr;
		cs(0);
		spi_byte(0x03);
		spi_byte((uint8_t)(addr >> 16));
		spi_byte((uint8_t)(addr >> 8));
		spi_byte((uint8_t)addr);
		for (uint32_t i = 0; i < len; i++)
			BUF[i] = spi_byte(0);
		cs(1);
		p->result = len;
	} else if (p->cmd == CMD_COPY) {
		/* copy memory with CPU loads, for areas the ROM's USB read can't see (e.g. the ROM at 0x02000000) */
		uint32_t len = p->len > BUF_MAX ? BUF_MAX : p->len;
		const volatile uint32_t *src = (const volatile uint32_t *)(uintptr_t)(p->addr & ~3u);
		volatile uint32_t *dst = (volatile uint32_t *)(uintptr_t)BUF;
		for (uint32_t i = 0; i < len / 4; i++)
			dst[i] = src[i];
		p->result = len;
	} else {
		p->result = 0xBAD1u;
	}
	p->done = DONE;
}
