/* Open-UV5R-Mini: ST7735S 128x128 RGB565 LCD.
 *
 * The init sequence and window offset are the stock firmware's (sub_01E485EC, sub_01E3E518).
 * The stock drives the panel with SPI2 at 20 MHz; this first port bit-bangs the same lines so it does not
 * depend on SPI2 register bits that are not decoded yet. CS = PB5, RESET = PB4, D/C = PR1 (power domain),
 * SCK/MOSI = PB0/PB1 (inferred from the board init; check on hardware).
 */
#include "driver/st7735s.h"
#include "board.h"
#include "driver/gpio.h"
#include "driver/system.h"

#define COL_OFFSET 32 /* MADCTL 0xA8 puts the 128 visible columns at 32..159 */

static void LCD_SetDC(bool data)
{
	uint8_t out = P33_Read(P33_PR_OUT);
	if (data)
		out |= 1u << LCD_DC_PR_BIT;
	else
		out &= ~(1u << LCD_DC_PR_BIT);
	P33_Write(P33_PR_OUT, out);
}

static void LCD_SendByte(uint8_t b)
{
	for (uint8_t bit = 0x80; bit; bit >>= 1) {
		GPIO_ClearBit(PIN_LCD_SCK);
		GPIO_Write(PIN_LCD_MOSI, b & bit);
		GPIO_SetBit(PIN_LCD_SCK);
	}
}

static void LCD_Command(uint8_t cmd)
{
	GPIO_ClearBit(PIN_LCD_CS);
	LCD_SetDC(false);
	LCD_SendByte(cmd);
	GPIO_SetBit(PIN_LCD_CS);
}

static void LCD_Data(const uint8_t *data, uint8_t len)
{
	GPIO_ClearBit(PIN_LCD_CS);
	LCD_SetDC(true);
	while (len--)
		LCD_SendByte(*data++);
	GPIO_SetBit(PIN_LCD_CS);
}

#define CMD(c, ...) do { static const uint8_t d_[] = { __VA_ARGS__ }; LCD_Command(c); LCD_Data(d_, sizeof(d_)); } while (0)

void ST7735S_SetBacklight(bool on)
{
	GPIO_Write(PIN_LCD_BACKLIGHT, on);
}

void ST7735S_Init(void)
{
	GPIO_ConfigOutput(PIN_LCD_CS, true);
	GPIO_ConfigOutput(PIN_LCD_RESET, true);
	GPIO_ConfigOutput(PIN_LCD_SCK, false);
	GPIO_ConfigOutput(PIN_LCD_MOSI, false);
	GPIO_ConfigOutput(PIN_LCD_BACKLIGHT, false);

	/* D/C (PR1) as output */
	P33_Write(P33_PR_DIR, P33_Read(P33_PR_DIR) & ~(1u << LCD_DC_PR_BIT));

	/* reset pulse as in the stock board init */
	GPIO_SetBit(PIN_LCD_RESET);
	SYSTEM_DelayMs(1);
	GPIO_ClearBit(PIN_LCD_RESET);
	SYSTEM_DelayMs(1);
	GPIO_SetBit(PIN_LCD_RESET);
	SYSTEM_DelayMs(60);

	LCD_Command(0x11);                                  /* SLPOUT */
	SYSTEM_DelayMs(120);
	CMD(0xB1, 0x05, 0x3A, 0x3A);                        /* FRMCTR1 */
	CMD(0xB2, 0x05, 0x3A, 0x3A);                        /* FRMCTR2 */
	CMD(0xB3, 0x05, 0x3A, 0x3A, 0x05, 0x3A, 0x3A);      /* FRMCTR3 */
	CMD(0xB4, 0x03);                                    /* INVCTR  */
	CMD(0xC0, 0x62, 0x02, 0x04);                        /* PWCTR1  */
	CMD(0xC1, 0xC0);                                    /* PWCTR2  */
	CMD(0xC2, 0x0D, 0x00);                              /* PWCTR3  */
	CMD(0xC3, 0x8D, 0x6A);                              /* PWCTR4  */
	CMD(0xC4, 0x8D, 0xEE);                              /* PWCTR5  */
	CMD(0xC5, 0x12);                                    /* VMCTR1  */
	CMD(0xE0, 0x03, 0x1B, 0x12, 0x11, 0x3F, 0x3A, 0x32, 0x34,
	          0x2F, 0x2B, 0x30, 0x3A, 0x00, 0x01, 0x02, 0x05); /* GMCTRP1 */
	CMD(0xE1, 0x03, 0x1B, 0x12, 0x11, 0x32, 0x2F, 0x2A, 0x2F,
	          0x2E, 0x2C, 0x35, 0x3F, 0x00, 0x00, 0x01, 0x05); /* GMCTRN1 */
	CMD(0xFC, 0x8C);                                    /* PWCTR6  */
	CMD(0x36, 0xA8);                                    /* MADCTL: MY | MV | BGR */
	CMD(0x3A, 0x05);                                    /* COLMOD: RGB565 */

	ST7735S_FillRect(0, 0, LCD_WIDTH, LCD_HEIGHT, 0x0000);
	LCD_Command(0x29);                                  /* DISPON */
	ST7735S_SetBacklight(true);
}

void ST7735S_SetWindow(uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
	uint8_t xs = COL_OFFSET + x, xe = COL_OFFSET + x + w - 1;
	uint8_t ys = y, ye = y + h - 1;
	uint8_t caset[4] = { 0, xs, 0, xe };
	uint8_t raset[4] = { 0, ys, 0, ye };

	LCD_Command(0x2A);
	LCD_Data(caset, 4);
	LCD_Command(0x2B);
	LCD_Data(raset, 4);
	LCD_Command(0x2C); /* RAMWR: pixels follow */
	GPIO_ClearBit(PIN_LCD_CS);
	LCD_SetDC(true);
}

void ST7735S_PushPixel(uint16_t color)
{
	LCD_SendByte(color >> 8);
	LCD_SendByte(color & 0xFF);
}

void ST7735S_FillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t color)
{
	ST7735S_SetWindow(x, y, w, h);
	for (uint16_t n = (uint16_t)w * h; n; n--)
		ST7735S_PushPixel(color);
	GPIO_SetBit(PIN_LCD_CS);
}
