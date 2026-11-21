/* Open-UV5R-Mini: internal flash access, run from RAM.
 *
 * The CPU runs from the internal flash through the flash controller (SFC, 0x1F0200). To send the flash its own
 * commands the controller is paused (CON bit 0 off) and the bytes go through SPI0 (0x1E1C00) with a GPIO as chip
 * select, as the stock RAM code does (docs/UPDATE-MODE.md). While the controller is paused nothing can be fetched from
 * the flash, so these functions live in .ramfunc (copied to RAM by start.S) and call nothing: every helper below is
 * a macro, and there are no tables or strings.
 *
 * What the stock code does, read from the stock app and the boot ROM (written fresh here):
 *   lock (stock 0x020001A0, then the boot ROM at 0x1127D4): wait 0x101000 bit 5 set, wait SFC CON bit 31 clear,
 *     SFC CON &= ~1, SPI0 CON |= 1 (SPI0 is off otherwise: our first try on a radio timed out without this).
 *   unlock (boot ROM 0x1127D8): SPI0 CON &= ~1, SFC CON |= 1.
 *   CS (stock 0x020001CA): DIR bit cleared, OUT bit low = selected; PD3, or PA13 when a flag at 0x198C0+1401 is set.
 *   byte out (0x02000226): CON |= 0x8, CON &= ~0x1000, BUF = byte, wait CON bit 15, CON |= 0x4000, CON &= ~0x8.
 *   byte in, one data line (0x020002BE, used for the status byte): CON |= 0x8 | 0x1000, BUF = 0xFF, wait CON bit 15,
 *     CON |= 0x4000, the byte is in BUF, CON &= ~0x8. (The bulk read for the two- and four-line modes leaves 0x8 off
 *     and sets 0x400/0x800; with 0x8 off our one-line read gave FFFFFF on a radio.)
 *   continuous read: SFC modes 6 and 7 (CON bits 8-11) keep the flash in a mode where it takes no command byte. We
 *   don't leave and re-enter it yet; FLASH_ReadId just reports it.
 * Every wait has a time-out, so a wrong guess comes back as an error rather than hanging (the watchdog would then
 * restart the radio).
 */
#include <stdint.h>
#include "bsp/ac695n.h"
#include "driver/flash.h"

#define SFC_CON  REG32(0x001F0200u)
#define SPI0_CON REG32(0x001E1C00u)
#define SPI0_BUF REG32(0x001E1C08u)

#define RAMFUNC __attribute__((section(".ramfunc"), noinline))

#define WAIT_LOOPS 200000u

#define SPI_WAIT(ok)                                       \
	do {                                                   \
		uint32_t n_ = WAIT_LOOPS;                          \
		while (!(SPI0_CON & 0x8000u))                      \
			if (!--n_) {                                   \
				(ok) = 0;                                  \
				break;                                     \
			}                                              \
		SPI0_CON |= 0x4000u;                               \
	} while (0)

#define SPI_OUT(b, ok)                                     \
	do {                                                   \
		SPI0_CON |= 0x8u;                                  \
		SPI0_CON &= ~0x1000u;                              \
		SPI0_BUF = (b);                                    \
		SPI_WAIT(ok);                                      \
		SPI0_CON &= ~0x8u;                                 \
	} while (0)

#define SPI_IN(v, ok)                                      \
	do {                                                   \
		SPI0_CON |= 0x8u | 0x1000u;                        \
		SPI0_BUF = 0xFFu;                                  \
		SPI_WAIT(ok);                                      \
		(v) = SPI0_BUF & 0xFFu;                            \
		SPI0_CON &= ~0x8u;                                 \
	} while (0)

RAMFUNC uint32_t FLASH_ReadId(uint32_t cs)
{
	volatile uint32_t *port = (volatile uint32_t *)(uintptr_t)(cs == FLASH_CS_PA13 ? 0x001E5000u : 0x001E50C0u);
	uint32_t bit = cs == FLASH_CS_PA13 ? (1u << 13) : (1u << 3);
	uint32_t ok = 1, id = 0, v, n;

	uint32_t mode = (SFC_CON >> 8) & 0xFu;
	if (mode == 6u || mode == 7u)
		return FLASH_ERR_CONTINUOUS;

	/* Hand the flash from the controller to SPI0, as the boot ROM's lock does (0x1127D4): wait for 0x101000 bit 5,
	 * wait for the controller to finish its current read, controller off, SPI0 on. */
	for (n = WAIT_LOOPS; !(REG32(0x00101000u) & 0x20u); )
		if (!--n)
			return FLASH_ERR_TIMEOUT;
	for (n = WAIT_LOOPS; SFC_CON & 0x80000000u; )
		if (!--n)
			return FLASH_ERR_TIMEOUT;
	uint32_t con = SFC_CON, spi = SPI0_CON;
	SFC_CON = con & ~1u;
	SPI0_CON = spi | 1u;

	uint32_t out = port[0], dir = port[2];
	port[0] = out | bit;    /* CS high (idle) */
	port[2] = dir & ~bit;   /* as output */
	port[0] &= ~bit;        /* CS low: selected */

	SPI_OUT(0x9Fu, ok);
	SPI_IN(v, ok); id = v << 16;
	SPI_IN(v, ok); id |= v << 8;
	SPI_IN(v, ok); id |= v;

	SPI0_CON &= ~0x1000u;
	port[0] |= bit;         /* CS high */
	port[2] = dir;
	port[0] = out;

	/* and back, as the boot ROM's unlock does (0x1127D8): SPI0 off, controller on */
	SPI0_CON = spi & ~1u;
	SFC_CON = con;
	return ok ? id : FLASH_ERR_TIMEOUT;
}
