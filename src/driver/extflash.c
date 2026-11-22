/* Open-UV5R-Mini: external SPI flash. See driver/extflash.h. */
#include "driver/extflash.h"
#include "store/settings.h"        /* SETTINGS_ENABLE_SAVE */
#include "board.h"
#include "driver/gpio.h"

#define PIN_XF_CS   IO_PORTA(3)
#define PIN_XF_MISO IO_PORTB(2)

void EXTFLASH_Init(void)
{
	GPIO_ConfigOutput(PIN_XF_CS, true);
	GPIO_ConfigInput(PIN_XF_MISO, true);
}

/* SPI mode 0, MSB first: data set while SCK is low, sampled on the rising edge. */
static uint8_t xfer(uint8_t out)
{
	uint8_t in = 0;

	for (int i = 7; i >= 0; i--) {
		GPIO_Write(PIN_LCD_MOSI, (out >> i) & 1u);
		GPIO_SetBit(PIN_LCD_SCK);
		in = (uint8_t)(in << 1 | (GPIO_CheckBit(PIN_XF_MISO) ? 1u : 0u));
		GPIO_ClearBit(PIN_LCD_SCK);
	}
	return in;
}

static void select(void)
{
	GPIO_SetBit(PIN_LCD_CS);           /* the LCD must not listen */
	GPIO_ClearBit(PIN_LCD_SCK);
	GPIO_ClearBit(PIN_XF_CS);
}

static void deselect(void)
{
	GPIO_SetBit(PIN_XF_CS);
}

uint32_t EXTFLASH_ReadId(void)
{
	uint32_t id;

	select();
	xfer(0x9F);
	id = (uint32_t)xfer(0) << 16;
	id |= (uint32_t)xfer(0) << 8;
	id |= xfer(0);
	deselect();
	return id;
}

void EXTFLASH_Read(uint32_t addr, uint8_t *buf, uint32_t len)
{
	select();
	xfer(0x03);
	xfer((uint8_t)(addr >> 16));
	xfer((uint8_t)(addr >> 8));
	xfer((uint8_t)addr);
	while (len--)
		*buf++ = xfer(0);
	deselect();
}

#if SETTINGS_ENABLE_SAVE
/* ------------------------------------------------------------------------------------------- writing (fenced)
 * Standard SPI NOR commands, as the stock app sends them (sub_01E40586 erase: write enable 06, 20 + address, then
 * status 05 until bit 0 clears; sub_01E404A6 program: 06, 02 + address + data). */
static int fenced(uint32_t addr, uint32_t len)
{
	return len && addr + 1u > EXTFLASH_WRITE_LO && addr < EXTFLASH_WRITE_HI && len <= EXTFLASH_WRITE_HI - addr;
}

static void command(uint8_t c)
{
	select();
	xfer(c);
	deselect();
}

static int wait_idle(void)
{
	for (uint32_t i = 0; i < 2000000u; i++) {
		uint8_t st;
		select();
		xfer(0x05);
		st = xfer(0);
		deselect();
		if (!(st & 1u))
			return 0;
	}
	return -1;
}

static void address(uint8_t c, uint32_t addr)
{
	xfer(c);
	xfer((uint8_t)(addr >> 16));
	xfer((uint8_t)(addr >> 8));
	xfer((uint8_t)addr);
}

int EXTFLASH_Erase4K(uint32_t addr)
{
	if ((addr & 0xFFFu) || !fenced(addr, 0x1000u) || EXTFLASH_ReadId() != EXTFLASH_ID)
		return -1;
	command(0x06);
	select();
	address(0x20, addr);
	deselect();
	return wait_idle();
}

static int program_page(uint32_t addr, const uint8_t *buf, uint32_t n)
{
	uint8_t back[16];

	command(0x06);
	select();
	address(0x02, addr);
	for (uint32_t i = 0; i < n; i++)
		xfer(buf[i]);
	deselect();
	if (wait_idle())
		return -1;
	for (uint32_t i = 0; i < n; i += sizeof back) {          /* read back */
		uint32_t m = n - i < sizeof back ? n - i : sizeof back;
		EXTFLASH_Read(addr + i, back, m);
		for (uint32_t j = 0; j < m; j++)
			if (back[j] != buf[i + j])
				return -1;
	}
	return 0;
}

int EXTFLASH_Program(uint32_t addr, const uint8_t *buf, uint32_t len)
{
	if (!fenced(addr, len) || EXTFLASH_ReadId() != EXTFLASH_ID)
		return -1;
	while (len) {
		uint32_t n = 0x100u - (addr & 0xFFu);
		if (n > len)
			n = len;
		if (program_page(addr, buf, n))
			return -1;
		addr += n;
		buf += n;
		len -= n;
	}
	return 0;
}
#endif
