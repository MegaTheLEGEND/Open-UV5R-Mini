/* Open-UV5R-Mini: ST7735S 128x128 RGB565 LCD.
 *
 * The init sequence and window offset are the stock firmware's (sub_01E485EC, sub_01E3E518). CS = PB5,
 * RESET = PB4, D/C = PR1 (power domain), SCK/MOSI = PB0/PB1 (SPI1 port A).
 *
 * As stock, the bytes go out through SPI1 with DMA (driver/spi1.c): pixels are collected in a RAM buffer and sent
 * while the next ones are drawn; a filled rectangle sends the same buffer again and again. If SPI1 didn't pass its
 * self-test (or ENABLE_LCD_SPI is 0), the same lines are bit-banged, as the first port did.
 */
#include "driver/st7735s.h"
#include "board.h"
#include "driver/gpio.h"
#include "driver/spi1.h"
#include "driver/system.h"

#define COL_OFFSET 32 /* MADCTL 0xA8 puts the 128 visible columns at 32..159 */
#define BUF_LEN    512u

static uint8_t  s_buf[2][BUF_LEN] __attribute__((aligned(4)));
static uint8_t  s_cur;                   /* the buffer being filled */
static uint16_t s_n;                     /* bytes in it */

static void LCD_SetDC(bool data)
{
	uint8_t out = P33_Read(P33_PR_OUT);
	if (data)
		out |= 1u << LCD_DC_PR_BIT;
	else
		out &= ~(1u << LCD_DC_PR_BIT);
	P33_Write(P33_PR_OUT, out);
}

/* SCK and MOSI share port B: read OUT once, then two plain stores per bit (clock low with the data, clock high). */
static void bang(const uint8_t *p, uint32_t n)
{
	volatile uint32_t *out = &GPIO_PIN_TO_PORT(PIN_LCD_SCK)->OUT;
	uint32_t base = *out & ~(GPIO_PIN_TO_BIT(PIN_LCD_SCK) | GPIO_PIN_TO_BIT(PIN_LCD_MOSI));
	uint32_t sck = GPIO_PIN_TO_BIT(PIN_LCD_SCK), mosi = GPIO_PIN_TO_BIT(PIN_LCD_MOSI);

	while (n--) {
		uint8_t b = *p++;
		for (uint8_t bit = 0x80; bit; bit >>= 1) {
			uint32_t v = base | (b & bit ? mosi : 0u);
			*out = v;
			*out = v | sck;
		}
	}
	*out = base;                         /* clock back low */
}

/* send n bytes from p; p must stay unchanged until the next send or LCD_Drain (DMA reads it) */
static void send(const uint8_t *p, uint32_t n)
{
	if (SPI1_Ok())
		SPI1_Send(p, n);
	if (!SPI1_Ok()) {                    /* never had it, or it just timed out */
		SPI1_Wait();
		bang(p, n);
	}
}

static void flush(void)
{
	if (s_n) {
		send(s_buf[s_cur], s_n);
		s_cur ^= 1u;                     /* fill the other one while this one goes out */
		s_n = 0;
	}
}

static void drain(void)
{
	flush();
	SPI1_Wait();
}

static void LCD_Command(uint8_t cmd)
{
	static uint8_t c;

	drain();
	GPIO_ClearBit(PIN_LCD_CS);
	LCD_SetDC(false);
	c = cmd;
	send(&c, 1);
	SPI1_Wait();
	GPIO_SetBit(PIN_LCD_CS);
}

static void LCD_Data(const uint8_t *data, uint8_t len)
{
	drain();
	GPIO_ClearBit(PIN_LCD_CS);
	LCD_SetDC(true);
	send(data, len);
	SPI1_Wait();
	GPIO_SetBit(PIN_LCD_CS);
}

#define CMD(c, ...) do { static const uint8_t d_[] = { __VA_ARGS__ }; LCD_Command(c); LCD_Data(d_, sizeof(d_)); } while (0)

void ST7735S_SetBacklight(bool on)
{
	GPIO_Write(PIN_LCD_BACKLIGHT, on);
}

/* Panel sleep for the radio's sleep mode: DISPOFF + SLPIN (the panel keeps its picture memory); waking is SLPOUT,
 * which needs 120 ms before the next command (ST7735S datasheet), then DISPON. */
void ST7735S_Sleep(bool on)
{
	if (on) {
		LCD_Command(0x28);                              /* DISPOFF */
		LCD_Command(0x10);                              /* SLPIN */
	} else {
		LCD_Command(0x11);                              /* SLPOUT */
		SYSTEM_DelayMs(120);
		LCD_Command(0x29);                              /* DISPON */
	}
}

void ST7735S_Init(void)
{
	s_n = 0;
	SPI1_Init();                                /* hardware path if its self-test passes; also sets CS, SCK, MOSI */
	GPIO_ConfigOutput(PIN_LCD_CS, true);
	GPIO_ConfigOutput(PIN_LCD_RESET, true);
	GPIO_ConfigOutput(PIN_LCD_BACKLIGHT, true); /* lit from the start: a lit but empty screen means the panel isn't answering */

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

bool ST7735S_HardwareSpi(void)
{
	return SPI1_Ok();
}

void ST7735S_SetWindow(uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
	static uint8_t caset[4], raset[4];       /* static: DMA reads them after we return */

	caset[0] = 0; caset[1] = (uint8_t)(COL_OFFSET + x); caset[2] = 0; caset[3] = (uint8_t)(COL_OFFSET + x + w - 1);
	raset[0] = 0; raset[1] = y; raset[2] = 0; raset[3] = (uint8_t)(y + h - 1);
	LCD_Command(0x2A);
	LCD_Data(caset, 4);
	LCD_Command(0x2B);
	LCD_Data(raset, 4);
	LCD_Command(0x2C); /* RAMWR: pixels follow until ST7735S_End */
	GPIO_ClearBit(PIN_LCD_CS);
	LCD_SetDC(true);
	s_n = 0;
}

void ST7735S_PushPixel(uint16_t color)
{
	uint8_t *b = s_buf[s_cur];

	b[s_n++] = (uint8_t)(color >> 8);
	b[s_n++] = (uint8_t)color;
	if (s_n >= BUF_LEN)
		flush();
}

void ST7735S_End(void)
{
	drain();
	GPIO_SetBit(PIN_LCD_CS);
}

void ST7735S_FillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t color)
{
	uint32_t n = (uint32_t)w * h * 2u;
	uint8_t *b;

	ST7735S_SetWindow(x, y, w, h);
	SPI1_Wait();                             /* the buffer below may still be going out */
	b = s_buf[s_cur];
	for (uint32_t i = 0; i < BUF_LEN && i < n; i += 2) {
		b[i] = (uint8_t)(color >> 8);
		b[i + 1] = (uint8_t)color;
	}
	while (n) {                              /* the same buffer again and again */
		uint32_t m = n < BUF_LEN ? n : BUF_LEN;
		send(b, m);
		n -= m;
	}
	SPI1_Wait();
	GPIO_SetBit(PIN_LCD_CS);
}
