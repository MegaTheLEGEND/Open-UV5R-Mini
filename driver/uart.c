/* Open-UV5R-Mini: cable UART, polled (no interrupt), DMA receive into a small buffer. See driver/uart.h. */
#include "driver/uart.h"
#include "driver/system.h"

#define UART_BASE   0x1E2100u
#define REG(o)      (*(volatile uint32_t *)(uintptr_t)(UART_BASE + (o)))
#define CON0        REG(0)
#define CON1        REG(4)
#define BAUD        REG(8)
#define OTCNT       REG(16)
#define TXADR       REG(20)
#define TXCNT       REG(24)
#define RXSADR      REG(28)
#define RXEADR      REG(32)
#define RXCNT       REG(36)
#define HRXCNT      REG(40)

#define CLOCK_HZ    48000000u
#define RXBUF_LEN   0x220u               /* the stock app's buffer size */

static uint8_t  s_rx[RXBUF_LEN] __attribute__((aligned(4)));
static uint8_t  s_tx[0x40] __attribute__((aligned(4)));
static uint32_t s_have, s_pos;           /* bytes in s_rx not yet handed out */

static void rx_arm(void)
{
	CON0 |= 0x1000;                      /* clear RX pending */
	RXSADR = (uint32_t)(uintptr_t)s_rx;
	RXEADR = (uint32_t)(uintptr_t)s_rx + RXBUF_LEN;
	RXCNT = RXBUF_LEN;
}

void UART_SetBaud(uint32_t baud)
{
	BAUD = CLOCK_HZ / baud / 4 - 1;
	OTCNT = CLOCK_HZ / baud * 20;        /* ~2 character times of silence ends a burst */
}

#define SFR(a)      (*(volatile uint32_t *)(uintptr_t)(a))
#define RMW(a, set, clr)  (SFR(a) = (SFR(a) & ~(uint32_t)(clr)) | (uint32_t)(set))

/* Put the UART on the USB pins: RX on 61 (D+), TX on 62 (D-). This is what the stock entry sub_01E492C0 does with
 * the GPIO helpers sub_01E001D2..sub_01E003B8 and the IO map sub_01E4856C / sub_01E484B6, as recorded by running it
 * in the emulator (emu/uart_pins.py). Each bit the stock code sets or clears is listed; every other bit is kept. */
static void route_usb_pins(void)
{
	SFR(0x1E1800u) = 0;                          /* USB controller off: the pins become GPIO */
	RMW(0x1E5100u, 0x00000F58u, 0x000000A7u);    /* USB-pin direction, pulls, input enable, function */
	RMW(0x1E5120u, 0x0000003Eu, 0x00000001u);    /* output channel 0 <- pin 62 */
	RMW(0x1E5124u, 0x00100040u, 0x00E00030u);    /* UART RX input <- pin 61 */
}

void UART_Init(uint32_t baud)
{
	CON1 = 0;
	CON0 = 0x3400;                       /* off, all pending flags cleared */
	UART_SetBaud(baud);
	rx_arm();
	route_usb_pins();
	s_have = s_pos = 0;
	CON0 |= 0x41;                        /* enable, RX DMA (stock: 0x6D, with the TX/RX/time-out irqs) */
}

void UART_Send(const uint8_t *data, uint32_t len)
{
	while (len) {
		uint32_t n = len < sizeof(s_tx) ? len : sizeof(s_tx);
		for (uint32_t i = 0; i < n; i++)
			s_tx[i] = data[i];
		TXADR = (uint32_t)(uintptr_t)s_tx;
		TXCNT = n;
		while (!(CON0 & 0x8000))
			;
		CON0 |= 0x2000;
		data += n;
		len -= n;
	}
}

int UART_Receive(uint8_t *buf, int max, uint32_t timeout_ms)
{
	int n = 0;

	for (uint32_t t = 0; s_pos == s_have; t++) {
		uint32_t got = 0;
		if (CON0 & 0x4000)                   /* buffer full */
			got = RXBUF_LEN;
		else if (CON0 & 0x0800) {            /* a burst ended */
			CON0 |= 0x0480;
			got = HRXCNT;
		}
		if (got) {
			s_have = got;
			s_pos = 0;
			rx_arm();                        /* (the stock ISR re-arms the same way) */
			break;
		}
		if (t >= timeout_ms)
			return 0;
		SYSTEM_DelayMs(1);
	}
	while (n < max && s_pos < s_have)
		buf[n++] = s_rx[s_pos++];
	return n;
}
