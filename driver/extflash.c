/* Open-UV5R-Mini: external SPI flash, read only. See driver/extflash.h. */
#include "driver/extflash.h"
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
