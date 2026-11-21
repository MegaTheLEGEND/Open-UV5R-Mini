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

/* The flash's chip select is PA13 (an output held high by the bootloader; the JEDEC ID read through it on a radio,
 * 2026-11-21: 856014, a Puya P25Q80, 1 MB). Low = selected. */
#define CS_LOW()  (GPIOA->OUT &= ~(1u << 13))
#define CS_HIGH() (GPIOA->OUT |= 1u << 13)

/* Only the app area that an update writes (0x1000..0x7C000) may be erased or programmed. The voice prompts, PRCT
 * (calibration) and VM (settings) above it are never touched (AGENTS.md rule 6). */
#define WRITABLE(a, n) ((a) >= 0x1000u && (a) < 0x7C000u && (n) <= 0x7C000u - (a))

#define BUSY_LOOPS 3000000u

static uint32_t s_con, s_spi;

/* Hand the flash from the controller to SPI0, as the boot ROM's lock does (0x1127D4): wait for 0x101000 bit 5,
 * wait for the controller to finish its current read, controller off, SPI0 on. */
static RAMFUNC uint32_t fl_lock(void)
{
	uint32_t n, mode = (SFC_CON >> 8) & 0xFu;

	if (mode == 6u || mode == 7u)
		return FLASH_ERR_CONTINUOUS;
	for (n = WAIT_LOOPS; !(REG32(0x00101000u) & 0x20u); )
		if (!--n)
			return FLASH_ERR_TIMEOUT;
	for (n = WAIT_LOOPS; SFC_CON & 0x80000000u; )
		if (!--n)
			return FLASH_ERR_TIMEOUT;
	s_con = SFC_CON;
	s_spi = SPI0_CON;
	SFC_CON = s_con & ~1u;
	SPI0_CON = s_spi | 1u;
	CS_HIGH();
	return FLASH_OK;
}

/* and back, as the boot ROM's unlock does (0x1127D8): SPI0 off, controller on */
static RAMFUNC void fl_unlock(void)
{
	CS_HIGH();
	SPI0_CON = (s_spi & ~1u) & ~0x1000u;
	SFC_CON = s_con;
}

/* command byte and, if addr_bytes, a 24-bit address, with CS left low */
static RAMFUNC uint32_t fl_cmd(uint8_t cmd, uint32_t addr, uint32_t with_addr)
{
	uint32_t ok = 1;

	CS_LOW();
	SPI_OUT(cmd, ok);
	if (with_addr) {
		SPI_OUT((uint8_t)(addr >> 16), ok);
		SPI_OUT((uint8_t)(addr >> 8), ok);
		SPI_OUT((uint8_t)addr, ok);
	}
	return ok;
}

static RAMFUNC uint32_t fl_status(uint32_t *st)
{
	uint32_t ok = fl_cmd(0x05u, 0, 0), v = 0xFFu;

	SPI_IN(v, ok);
	CS_HIGH();
	*st = v;
	return ok;
}

/* write enable, then wait until the flash says it's on (status bit 1) */
static RAMFUNC uint32_t fl_wren(void)
{
	uint32_t ok = fl_cmd(0x06u, 0, 0), st = 0;

	CS_HIGH();
	if (!ok || !fl_status(&st))
		return FLASH_ERR_TIMEOUT;
	return (st & 2u) ? FLASH_OK : FLASH_ERR_WREN;
}

/* wait for the end of an erase or program (status bit 0) */
static RAMFUNC uint32_t fl_wait_busy(void)
{
	uint32_t st;

	for (uint32_t n = BUSY_LOOPS; n; n--) {
		if (!fl_status(&st))
			return FLASH_ERR_TIMEOUT;
		if (!(st & 1u))
			return FLASH_OK;
	}
	return FLASH_ERR_TIMEOUT;
}

RAMFUNC uint32_t FLASH_ReadId(void)
{
	uint32_t r = fl_lock(), ok, v, id;

	if (r != FLASH_OK)
		return r;
	ok = fl_cmd(0x9Fu, 0, 0);
	SPI_IN(v, ok); id = v << 16;
	SPI_IN(v, ok); id |= v << 8;
	SPI_IN(v, ok); id |= v;
	fl_unlock();
	return ok ? id : FLASH_ERR_TIMEOUT;
}

RAMFUNC uint32_t FLASH_ReadStatus(void)
{
	uint32_t r = fl_lock(), st = 0;

	if (r != FLASH_OK)
		return r;
	r = fl_status(&st) ? st : FLASH_ERR_TIMEOUT;
	fl_unlock();
	return r;
}

RAMFUNC uint32_t FLASH_Read(uint32_t addr, uint8_t *buf, uint32_t len)
{
	uint32_t r = fl_lock(), ok, v;

	if (r != FLASH_OK)
		return r;
	ok = fl_cmd(0x03u, addr, 1);
	for (uint32_t i = 0; i < len; i++) {
		SPI_IN(v, ok);
		buf[i] = (uint8_t)v;
	}
	fl_unlock();
	return ok ? FLASH_OK : FLASH_ERR_TIMEOUT;
}

RAMFUNC uint32_t FLASH_Erase4K(uint32_t addr)
{
	uint32_t r, ok;

	if ((addr & 0xFFFu) || !WRITABLE(addr, 0x1000u))
		return FLASH_ERR_ADDR;
	r = fl_lock();
	if (r != FLASH_OK)
		return r;
	r = fl_wren();
	if (r == FLASH_OK) {
		ok = fl_cmd(0x20u, addr, 1);
		CS_HIGH();
		r = ok ? fl_wait_busy() : FLASH_ERR_TIMEOUT;
	}
	fl_unlock();
	return r;
}

RAMFUNC uint32_t FLASH_Program(uint32_t addr, const uint8_t *buf, uint32_t len)
{
	uint32_t r = FLASH_OK, ok;

	if (!WRITABLE(addr, len))
		return FLASH_ERR_ADDR;
	r = fl_lock();
	if (r != FLASH_OK)
		return r;
	while (len && r == FLASH_OK) {
		uint32_t n = 0x100u - (addr & 0xFFu);   /* up to the end of this 256-byte page */
		if (n > len)
			n = len;
		r = fl_wren();
		if (r != FLASH_OK)
			break;
		ok = fl_cmd(0x02u, addr, 1);
		for (uint32_t i = 0; i < n; i++)
			SPI_OUT(buf[i], ok);
		CS_HIGH();
		r = ok ? fl_wait_busy() : FLASH_ERR_TIMEOUT;
		addr += n;
		buf += n;
		len -= n;
	}
	fl_unlock();
	return r;
}
