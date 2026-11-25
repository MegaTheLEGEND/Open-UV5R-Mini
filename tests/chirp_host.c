/* PC harness for src/app/program.c (CHIRP programming), driven from tests/chirp_host.py: a fake external flash that
 * behaves like NOR (program only clears bits, erase sets a 4 KB sector to FF) and refuses what the real driver
 * refuses, and a fake UART (with PROGRAM_BT also a fake SPP link) as two byte queues the Python side fills and
 * empties while PROGRAM_Poll runs. */
#include <stdint.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#define SLEEP_MS(n) Sleep(n)
#define EXPORT __declspec(dllexport)
#else
#include <unistd.h>
#define SLEEP_MS(n) usleep((n) * 1000)
#define EXPORT
#endif
#include "driver/extflash.h"

static uint8_t flash[0x200000];
static volatile uint32_t erases, programs, refused, bad_program;

void EXTFLASH_Read(uint32_t addr, uint8_t *buf, uint32_t len) { memcpy(buf, flash + addr, len); }

int EXTFLASH_Erase4K(uint32_t addr)
{
	if ((addr & 0xFFFu) || addr >= EXTFLASH_WRITE_HI) {
		refused++;
		return -1;
	}
	memset(flash + addr, 0xFF, 0x1000);
	erases++;
	return 0;
}

int EXTFLASH_Program(uint32_t addr, const uint8_t *buf, uint32_t len)
{
	if (!len || addr + len > EXTFLASH_WRITE_HI) {
		refused++;
		return -1;
	}
	for (uint32_t i = 0; i < len; i++) {
		flash[addr + i] &= buf[i];
		if (flash[addr + i] != buf[i]) {
			bad_program++;                /* programming over bits not erased */
			return -1;
		}
	}
	programs++;
	return 0;
}

#ifdef _WIN32
static uint64_t now_ms(void) { return GetTickCount64(); }
#else
#include <time.h>
static uint64_t now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u; }
#endif

#define Q 0x10000u
/* Two threads share each queue (Python writes, PROGRAM_Poll reads, and back): the byte is stored before the count
 * moves on, both volatile, so the reader never sees a count ahead of its byte. With q_in[in_w++ % Q] = b the
 * compiler could move the count first, and a download now and then came back with one wrong byte. */
static volatile uint8_t q_in[Q], q_out[Q];
static volatile uint32_t in_w, in_r, out_w, out_r;
static void put_out(uint8_t b) { q_out[out_w % Q] = b; out_w = out_w + 1; }

void UART_Init(uint32_t baud) { (void)baud; }
static volatile uint32_t uart_sends;
void UART_Send(const uint8_t *p, uint32_t n)
{
	uart_sends++;
	for (uint32_t i = 0; i < n; i++)
		put_out(p[i]);
}
int UART_Receive(uint8_t *buf, int max, uint32_t timeout_ms)
{
	uint64_t end = now_ms() + timeout_ms;        /* real time: Sleep(1) is ~15 ms on Windows */
	while (in_r == in_w) {
		if (now_ms() >= end)
			return 0;
		SLEEP_MS(1);
	}
	int n = 0;
	while (n < max && in_r != in_w) {
		buf[n++] = q_in[in_r % Q];
		in_r = in_r + 1;
	}
	return n;
}
void program_test_screen(const char *what) { (void)what; }
void program_test_wait_ms(uint32_t ms)
{
	uint64_t end = now_ms() + ms;
	while (now_ms() < end)
		SLEEP_MS(1);
}

/* The Bluetooth link (built with PROGRAM_BT): what the SPP glue does on the radio, received bytes go to
 * PROGRAM_BtReceive and replies come out of BT_Send. Replies land in the same queue the Python side reads; the
 * cable's UART_Send must not be used during a Bluetooth session, which test_count(4) counts. */
#if PROGRAM_BT
#include "bt/bt.h"
#include "app/program.h"
int BT_Send(const uint8_t *data, uint16_t len)
{
	for (uint16_t i = 0; i < len; i++)
		put_out(data[i]);
	return 0;
}
EXPORT void test_feed_bt(const uint8_t *p, int n) { PROGRAM_BtReceive(p, (uint16_t)n); }
#endif

int PROGRAM_Poll(void);
EXPORT int test_poll(void) { return PROGRAM_Poll(); }
EXPORT uint8_t *test_flash(void) { return flash; }
EXPORT void test_feed(const uint8_t *p, int n)
{
	for (int i = 0; i < n; i++) {
		q_in[in_w % Q] = p[i];
		in_w = in_w + 1;
	}
}
EXPORT int test_take(uint8_t *p, int max)
{
	int n = 0;
	while (n < max && out_r != out_w) {
		p[n++] = q_out[out_r % Q];
		out_r = out_r + 1;
	}
	return n;
}
EXPORT uint32_t test_count(int which)
{
	return which == 0 ? erases : which == 1 ? programs : which == 2 ? refused : which == 3 ? bad_program : uart_sends;
}

/* Start-up state: gStock from the flash, as APP_Init does; with an empty flash, seed both logs first. */
#include "store/settings.h"
EXPORT int test_load(void) { return SETTINGS_Load(); }
EXPORT int test_seed(void)
{
	static const uint8_t vfo[32] = { 1, 4, 6, 5, 2, 0, 0, 0 };     /* 146.52000 MHz, everything else 0 */
	uint8_t rec[64];

	for (int i = 0; i < 64; i++)
		rec[i] = vfo[i & 31];
	SETTINGS_Defaults();
	return SETTINGS_SaveSettings() || SETTINGS_SaveVfoRaw(rec);
}
