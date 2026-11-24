/* Open-UV5R-Mini: SPI1 hardware with DMA. See driver/spi1.h. */
#include "driver/spi1.h"
#include "board.h"
#include "driver/extflash.h"
#include "driver/gpio.h"

#define SPI_CON   REG32(SPI1_BASE + 0x00u)
#define SPI_BAUD  REG32(SPI1_BASE + 0x04u)
#define SPI_BUF   REG32(SPI1_BASE + 0x08u)
#define SPI_ADR   REG32(SPI1_BASE + 0x0Cu)
#define SPI_CNT   REG32(SPI1_BASE + 0x10u)
#define IOMAP_CON1 REG32(0x001E511Cu)

#define CON_EN      0x0001u
#define CON_SMP     0x0010u
#define CON_UPD     0x0020u
#define CON_CSIDLE  0x0080u
#define CON_RX      0x1000u
#define CON_PCLR    0x4000u
#define CON_PND     0x8000u

/* Clock / (BAUD + 1). Our app leaves the clocks as the bootloader set them (the UART driver counts on 48 MHz): 3 gives
 * 12 MHz there, under the panel's limit even if the clock were twice that. Stock runs 20 MHz. */
#define BAUD        3u
#define WAIT_LOOPS  400000u               /* a 512-byte DMA at 12 MHz is ~0.35 ms; this is far longer */

static bool s_ok, s_busy;

bool SPI1_Ok(void) { return s_ok; }

static void off(void)
{
	SPI_CON = 0;                           /* pins back to their GPIO settings */
	s_ok = s_busy = false;
}

static bool wait_pnd(void)
{
	for (uint32_t i = 0; i < WAIT_LOOPS; i++)
		if (SPI_CON & CON_PND) {
			SPI_CON |= CON_PCLR;
			return true;
		}
	off();                                 /* never again this power-on: the bit-banged path takes over */
	return false;
}

void SPI1_Wait(void)
{
	if (s_busy) {
		s_busy = false;
		wait_pnd();
	}
}

void SPI1_Put(uint8_t out)
{
	SPI1_Wait();
	if (!s_ok)
		return;
	SPI_CON &= ~CON_RX;
	SPI_BUF = out;
	wait_pnd();
}

/* as JieLi's spi_recv_byte: receive mode, a dummy 0xFF written to start the clock, then BUF holds the byte */
uint8_t SPI1_Get(void)
{
	SPI1_Wait();
	if (!s_ok)
		return 0xFF;
	SPI_CON |= CON_RX;
	SPI_BUF = 0xFF;
	if (!wait_pnd())
		return 0xFF;
	return (uint8_t)SPI_BUF;
}

void SPI1_Send(const uint8_t *p, uint32_t n)
{
	SPI1_Wait();
	if (!s_ok || !n)
		return;
	SPI_CON &= ~CON_RX;
	SPI_ADR = (uint32_t)(uintptr_t)p;
	SPI_CNT = n;                           /* starts the transfer */
	s_busy = true;
}

bool SPI1_Init(void)
{
	uint32_t id;

#if !LCD_HW_SPI
	return false;
#endif
	SPI1_Wait();
	off();
	GPIO_ConfigOutput(PIN_LCD_CS, true);           /* nobody listens while we set up */
	GPIO_ConfigOutput(PIN_XF_CS, true);
	GPIO_ConfigOutput(PIN_LCD_SCK, false);
	GPIO_ConfigOutput(PIN_LCD_MOSI, false);
	GPIO_ConfigInput(PIN_XF_MISO, true);
	IOMAP_CON1 &= ~0x10u;                          /* SPI1 on port A: PB0 / PB1 / PB2 */
	/* as JieLi's spi_open: master, CS pin unused, 8 bits, edges as theirs, clock idle low; one direction at a time
	 * (bit 12), as stock sends to the LCD and reads the flash */
	SPI_CON = CON_SMP | CON_UPD | CON_CSIDLE | CON_PCLR;
	SPI_BAUD = BAUD;
	SPI_BUF = 0;                                   /* data line low after enabling */
	SPI_CON |= CON_EN;
	SPI_CON |= CON_PCLR;                           /* no stale pending flag before the first byte */
	s_ok = true;

	/* self-test: the flash's JEDEC ID through the hardware (the LCD's CS stays high) */
	GPIO_ClearBit(PIN_XF_CS);
	SPI1_Put(0x9F);
	id = (uint32_t)SPI1_Get() << 16;
	id |= (uint32_t)SPI1_Get() << 8;
	id |= SPI1_Get();
	GPIO_SetBit(PIN_XF_CS);
	if (!s_ok || id != EXTFLASH_ID)
		off();
	return s_ok;
}
