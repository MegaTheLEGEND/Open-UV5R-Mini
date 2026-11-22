/* PC test of driver/uart.c's receive path against a model of the JieLi UART (tests/uart_host.py builds and runs it).
 *
 * The model: bytes arrive by DMA into RXSADR.., one per simulated millisecond (about 9600 baud). A write of CON0 bit 7
 * (RDC) latches the count into HRXCNT; the driver always re-arms right after, which restarts the DMA at the buffer
 * start (as the stock ISR relies on). The time-out flag (bit 11) comes 2 ms after the last byte, unless a case turns
 * it off. TX completes at once.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "driver/uart.h"

volatile uint32_t uart_test_regs[16];
volatile uint32_t uart_test_sfr[4];

static uint8_t  in_q[4096];
static uint32_t in_at[4096];
static int      in_n, in_i;
static uint32_t now, last_byte_at, dma_pos;
static int      ot_works = 1;

#define CON0   uart_test_regs[0]
#define RXSADR uart_test_regs[7]
#define HRXCNT uart_test_regs[10]

static uint8_t *dma_base(void)
{
	extern uint8_t *uart_test_rxbuf(void);
	return uart_test_rxbuf();
}

static void deliver(void)
{
	while (in_i < in_n && in_at[in_i] <= now) {
		if (dma_pos < 0x220) {
			dma_base()[dma_pos++] = in_q[in_i];
			if (dma_pos == 0x220)
				CON0 |= 0x4000;
		}
		in_i++;
		last_byte_at = now;
	}
	if (ot_works && dma_pos && now - last_byte_at >= 2)
		CON0 |= 0x0800;
}

void uart_test_delay_ms(uint32_t ms)
{
	while (ms--) {
		now++;
		deliver();
	}
}

/* RDC latches the count; the clear bits written with it clear their flags. Re-arming restarts the DMA: a byte that
 * arrives between the latch and the re-arm is lost, as on the chip. */
void uart_test_rdc(void)
{
	HRXCNT = dma_pos;
	if (CON0 & 0x0400u)
		CON0 &= ~0x0800u;
	CON0 &= ~(0x0080u | 0x0400u);
}

void uart_test_rearm(void)
{
	dma_pos = 0;
	CON0 &= ~(0x4000u | 0x1000u);
}

static void queue(const uint8_t *b, int n, uint32_t start, uint32_t gap)
{
	for (int i = 0; i < n; i++) {
		in_q[in_n] = b[i];
		in_at[in_n++] = start + (uint32_t)i * gap;
	}
}

static void reset(void)
{
	memset((void *)uart_test_regs, 0, sizeof uart_test_regs);
	in_n = in_i = 0;
	now = last_byte_at = dma_pos = 0;
	ot_works = 1;
	HRXCNT = 0x69;                  /* stale count left by the bootloader: must never be trusted */
	UART_Init(9600);
	CON0 |= 0x8000;                 /* TX always done in this model */
}

static int read_all(uint8_t *out, int want, uint32_t timeout)
{
	int got = 0;
	while (got < want) {
		int n = UART_Receive(out + got, want - got, timeout);
		if (n <= 0)
			break;
		got += n;
	}
	return got;
}

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
	static const uint8_t ping[7] = { 0xAA, 0x55, 0x01, 0x00, 0x06, 0x12, 0x34 };
	uint8_t buf[2048];

	/* 1. nothing on the line: no stale bytes come out */
	reset();
	CHECK(UART_Receive(buf, 64, 100) == 0, "stale HRXCNT gave bytes with nothing on the line");

	/* 2. one ping with the time-out flag */
	reset();
	queue(ping, 7, 5, 1);
	CHECK(read_all(buf, 7, 200) == 7 && !memcmp(buf, ping, 7), "ping with time-out flag");

	/* 3. no time-out flag at all: the backstop still hands the bytes on */
	reset();
	ot_works = 0;
	queue(ping, 7, 5, 1);
	CHECK(read_all(buf, 7, 200) == 7 && !memcmp(buf, ping, 7), "ping without time-out flag");

	/* 4. fifteen pings in a row, flag missing (the 105-byte stall on a radio) */
	reset();
	ot_works = 0;
	for (int i = 0; i < 15; i++)
		queue(ping, 7, 5 + 10u * (uint32_t)i, 1);
	{
		int n = read_all(buf, 105, 300), ok = n == 105;
		for (int i = 0; ok && i < 15; i++)
			ok = !memcmp(buf + 7 * i, ping, 7);
		CHECK(ok, "15 pings without flag: got %d bytes", n);
	}

	/* 5. a long reply (8 + 512 data) bigger than half the buffer, then the buffer-full case */
	reset();
	{
		static uint8_t big[0x300];
		for (int i = 0; i < (int)sizeof big; i++)
			big[i] = (uint8_t)(i * 7 + 1);
		queue(big, sizeof big, 3, 0);              /* all at once: fills the DMA buffer */
		uart_test_delay_ms(3);
		int n = read_all(buf, 0x220, 100);
		CHECK(n == 0x220 && !memcmp(buf, big, 0x220), "buffer-full burst: got %d", n);
	}

	/* 6. echo: bytes that arrive while we send are dropped, the host's answer after it is kept */
	reset();
	queue(ping, 7, 1, 0);                          /* lands during the send below */
	uart_test_delay_ms(1);
	UART_Send(ping, 7);
	queue((const uint8_t *)"\xAA\x55\x05\x00\x02", 5, now + 3, 1);
	CHECK(read_all(buf, 5, 100) == 5 && buf[4] == 0x02, "echo dropped, answer kept");
	CHECK(UART_Dropped() >= 7, "echo bytes counted as dropped");

	/* 7. the host answers at once: its frame lands while we are still sending, after our echo; it must be kept */
	reset();
	{
		uint8_t both[7 + 2 + 5];
		memcpy(both, ping, 7);                      /* our echo */
		both[7] = 0xFF; both[8] = 0xFF;             /* line noise */
		{ static const uint8_t r[5] = { 0xAA, 0x55, 0x05, 0x00, 0x01 }; memcpy(both + 9, r, 5); }  /* the host's rate frame starts */
		queue(both, sizeof both, 1, 0);
		uart_test_delay_ms(1);
		UART_Send(ping, 7);
		CHECK(read_all(buf, 5, 100) == 5 && buf[0] == 0xAA && buf[4] == 0x01, "host frame during our send kept");
	}

	/* 8. no echo, and the host's reply starts like our request (AA 55 xx 00): nothing of it may be dropped */
	reset();
	{
		static const uint8_t req[7] = { 0xAA, 0x55, 0x09, 0x00, 0x02, 0x00, 0x00 };
		static const uint8_t rep[7] = { 0xAA, 0x55, 0x49, 0x00, 0x02, 0x00, 0x00 };
		queue(rep, sizeof rep, 1, 0);
		uart_test_delay_ms(1);
		UART_Send(req, sizeof req);
		CHECK(read_all(buf, 7, 100) == 7 && !memcmp(buf, rep, 7), "reply with the request's prefix kept whole");
	}

	/* 9. a 527-byte reply trickling in with no time-out flag (as on a radio): every byte must arrive */
	reset();
	ot_works = 0;
	{
		static uint8_t big[527];
		for (int i = 0; i < (int)sizeof big; i++)
			big[i] = (uint8_t)(i * 7 + 1);
		queue(big, sizeof big, 1, 1);               /* one byte a millisecond, no gaps */
		CHECK(read_all(buf, sizeof big, 2000) == (int)sizeof big && !memcmp(buf, big, sizeof big),
		      "long burst with no time-out flag: no byte lost");
	}
	ot_works = 1;

	printf(fails ? "uart_host: %d FAILED\n" : "uart_host: all passed\n", fails);
	return fails != 0;
}
