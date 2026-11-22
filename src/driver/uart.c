/* Open-UV5R-Mini: cable UART, polled (no interrupt), DMA receive. See driver/uart.h.
 *
 * Receive follows JieLi's own UART pattern (the stock ISR 0x01E5949E and the BR23 SDK uart driver):
 *   - HRXCNT holds a count only after CON0 bit 7 ("RDC") is written: it latches the DMA's byte count. Read without
 *     it, HRXCNT is stale (our 690b169 build read it that way as a stall backstop and went silent on a radio).
 *   - A burst ends with the time-out flag (bit 11, cleared with bit 10) or the buffer-full flag (bit 14, cleared with
 *     bit 12). Then: RDC, take the count, copy the bytes out at once, re-arm the DMA at the buffer start and reload
 *     OTCNT, as stock does. The bytes go into a FIFO, so the DMA can never overwrite bytes not yet handed out.
 *   - Backstop: every 20 ms without a flag the same RDC-and-copy runs anyway, so bytes can't sit unseen if the
 *     time-out flag doesn't come (seen on a radio 2026-11-21: 105 bytes waited with no flag).
 *   - After a send, only leading FF noise and a whole copy of what we sent (an echo) are dropped; everything else is
 *     kept. A partial match is not an echo: our read request and the host's reply both start AA 55 xx 00, and
 *     dropping a matching prefix cut the start off every reply (radio, 2026-11-21). Dropping everything lost the flasher's rate frame on a radio
 *     (2026-11-21): the host answers at once, and the TX-done wait can outlast our last byte. Stray FF bytes were
 *     seen on a radio.
 * Every wait has a time-out.
 *
 * For the PC test (tests/uart_host.py) UART_HOST_TEST swaps the registers for an array and the delay for a hook.
 */
#include "driver/uart.h"

#ifdef UART_HOST_TEST
extern volatile uint32_t uart_test_regs[16];
extern volatile uint32_t uart_test_sfr[4];
void uart_test_delay_ms(uint32_t ms);
void uart_test_rdc(void);
#define REG(o)            (uart_test_regs[(o) / 4])
#define SFR_USB_CON       uart_test_sfr[0]
#define SFR_IOMAP0        uart_test_sfr[1]
#define SFR_IOMAP_OUT     uart_test_sfr[2]
#define SFR_IOMAP_IN      uart_test_sfr[3]
#define DELAY_MS(n)       uart_test_delay_ms(n)
#define ADDR(p)           ((uint32_t)(uintptr_t)(p) & 0xFFFFFFFFu)
#define AFTER_RDC()       uart_test_rdc()
#else
#include "driver/system.h"
#define UART_BASE         0x1E2100u
#define REG(o)            (*(volatile uint32_t *)(uintptr_t)(UART_BASE + (o)))
#define SFR(a)            (*(volatile uint32_t *)(uintptr_t)(a))
#define SFR_USB_CON       SFR(0x1E1800u)
#define SFR_IOMAP0        SFR(0x1E5100u)
#define SFR_IOMAP_OUT     SFR(0x1E5120u)
#define SFR_IOMAP_IN      SFR(0x1E5124u)
#define DELAY_MS(n)       SYSTEM_DelayMs(n)
#define ADDR(p)           ((uint32_t)(uintptr_t)(p))
#define AFTER_RDC()       __asm__ volatile("nop")
#endif

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

#define CON_EN      0x0001u
#define CON_RXDMA   0x0040u
#define CON_RDC     0x0080u
#define CON_CLR_OT  0x0400u
#define CON_OT      0x0800u
#define CON_CLR_RX  0x1000u
#define CON_CLR_TX  0x2000u
#define CON_RXFULL  0x4000u
#define CON_TXDONE  0x8000u

#define CLOCK_HZ    48000000u
#define RXBUF_LEN   0x220u               /* the stock app's DMA buffer size */
#define FIFO_LEN    0x400u               /* power of two */
#define BACKSTOP_MS 20u
#define TX_WAIT     2000000u             /* loops; 64 bytes at 1200 baud is ~0.5 s */

static uint8_t  s_rx[RXBUF_LEN] __attribute__((aligned(4)));
static uint8_t  s_tx[0x40] __attribute__((aligned(4)));
static uint8_t  s_fifo[FIFO_LEN];
static uint32_t s_head, s_tail;          /* FIFO write and read counts */
static uint32_t s_tx_n;                          /* bytes in s_tx: the last chunk sent, for the echo check */
static uint32_t s_otcnt, s_quiet_ms, s_dropped;

static void rx_arm(void)
{
	CON0 |= CON_CLR_RX;
	RXSADR = ADDR(s_rx);
	RXEADR = ADDR(s_rx) + RXBUF_LEN;
	RXCNT = RXBUF_LEN;
	OTCNT = s_otcnt;
}

/* Latch the DMA count, clear the flags, move the bytes into the FIFO and re-arm. echo: drop our own echo and FF noise
 * at the start (see above). */
static void rx_collect(int echo)
{
	uint32_t n, i = 0;

	CON0 |= CON_RDC;
	CON0 |= CON_CLR_OT;
	AFTER_RDC();
	n = HRXCNT;
	if (n > RXBUF_LEN)
		n = RXBUF_LEN;
	if (echo) {
		while (i < n && s_rx[i] == 0xFF)
			i++, s_dropped++;
		if (s_tx_n && n - i >= s_tx_n) {
			uint32_t k = 0;
			while (k < s_tx_n && s_rx[i + k] == s_tx[k])
				k++;
			if (k == s_tx_n)
				i += k, s_dropped += k;
		}
		while (i < n && s_rx[i] == 0xFF)
			i++, s_dropped++;
	}
	for (; i < n; i++) {
		if (s_head - s_tail < FIFO_LEN)
			s_fifo[s_head++ & (FIFO_LEN - 1)] = s_rx[i];
		else
			s_dropped++;
	}
	rx_arm();
	s_quiet_ms = 0;
}

/* One look at the receiver; called about once a millisecond while waiting. */
static void rx_poll(void)
{
	if (CON0 & (CON_OT | CON_RXFULL))
		rx_collect(0);
	else if (++s_quiet_ms >= BACKSTOP_MS)
		rx_collect(0);
}

void UART_SetBaud(uint32_t baud)
{
	BAUD = CLOCK_HZ / baud / 4 - 1;
	s_otcnt = CLOCK_HZ / baud * 20;      /* ~2 character times of silence ends a burst */
	OTCNT = s_otcnt;
}

/* Put the UART on the USB pins: RX on 61 (D+), TX on 62 (D-). This is what the stock entry sub_01E492C0 does with
 * the GPIO helpers sub_01E001D2..sub_01E003B8 and the IO map sub_01E4856C / sub_01E484B6, as recorded by running it
 * in the emulator (emu/uart_pins.py). Each bit the stock code sets or clears is listed; every other bit is kept. */
#define RMW(r, set, clr)  ((r) = ((r) & ~(uint32_t)(clr)) | (uint32_t)(set))
static void route_usb_pins(void)
{
	SFR_USB_CON = 0;                             /* USB controller off: the pins become GPIO */
	RMW(SFR_IOMAP0, 0x00000F58u, 0x000000A7u);   /* USB-pin direction, pulls, input enable, function */
	RMW(SFR_IOMAP_OUT, 0x0000003Eu, 0x00000001u); /* output channel 0 <- pin 62 */
	RMW(SFR_IOMAP_IN, 0x00100040u, 0x00E00030u); /* UART RX input <- pin 61 */
}

void UART_Init(uint32_t baud)
{
	CON1 = 0;
	CON0 = CON_CLR_TX | CON_CLR_RX | CON_CLR_OT;  /* off, all pending flags cleared */
	UART_SetBaud(baud);
	rx_arm();
	route_usb_pins();
	s_head = s_tail = s_quiet_ms = 0;
	CON0 |= CON_EN | CON_RXDMA;                   /* stock: 0x6D, with the TX/RX/time-out irqs; we poll */
}

void UART_Send(const uint8_t *data, uint32_t len)
{
	rx_poll();                                    /* keep what came before we started talking */
	while (len) {
		uint32_t n = len < sizeof(s_tx) ? len : sizeof(s_tx);
		for (uint32_t i = 0; i < n; i++)
			s_tx[i] = data[i];
		CON0 |= CON_CLR_TX;
		TXADR = ADDR(s_tx);
		s_tx_n = n;
		TXCNT = n;
		for (uint32_t w = TX_WAIT; !(CON0 & CON_TXDONE) && w; w--)
			;
		CON0 |= CON_CLR_TX;
		data += n;
		len -= n;
	}
	rx_collect(1);                                /* drop our echo and FF noise, keep the host's bytes */
}

int UART_Receive(uint8_t *buf, int max, uint32_t timeout_ms)
{
	int n = 0;

	for (uint32_t t = 0; s_head == s_tail; t++) {
		rx_poll();
		if (s_head != s_tail)
			break;
		if (t >= timeout_ms)
			return 0;
		DELAY_MS(1);
	}
	while (n < max && s_tail != s_head)
		buf[n++] = s_fifo[s_tail++ & (FIFO_LEN - 1)];
	return n;
}

uint32_t UART_Reg(uint32_t offset)
{
	return REG(offset);
}

uint32_t UART_Dropped(void)
{
	return s_dropped;
}

#ifdef UART_HOST_TEST
uint8_t *uart_test_rxbuf(void) { return s_rx; }
#endif
