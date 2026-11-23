/* Open-UV5R-Mini: programming over the cable in normal mode (roadmap 5.3), so CHIRP's existing UV-5R Mini driver
 * (chirp/drivers/baofeng_uv17Pro.py, class UV5RMini) works unchanged.
 *
 * The protocol, as that driver speaks it (115200 8N1, the same cable UART on the USB pins as the update mode):
 *   PC "PROGRAMCOLORPROU"           radio 06                (stock compares it in sub_01E3DFF0)
 *   PC "F"                          radio 16 bytes          (stock: the band limits at external 0xF230, sub_01E4CCDA)
 *   PC "M"                          radio 15 bytes          (stock 0x01E4D00A: 6 bytes from RAM, "  +L", then
 *                                                            external 0xF251-0xF255; CHIRP ignores the content)
 *   PC "SEND!" + 20 bytes           radio 06
 *   PC "R" addr16 len8              radio "R" addr16 len8 + len bytes, scrambled
 *   PC "W" addr16 len8 + len bytes  radio 06 (anything else: CHIRP stops with "bad ack")
 *   PC "E"                          end (stock); CHIRP just stops, so 3 s of quiet ends the session too.
 * Scrambling: XOR with "CO 7" (CHIRP _crypt, symbol 1), byte by byte cycling the 4 key bytes, except where the key
 * byte is a space or the data byte is 00, FF, the key byte or its inverse. It is its own inverse.
 *
 * Addresses are CHIRP's, which are the stock external-flash addresses: channels 0x0000-0x803F (999 x 32 bytes; the
 * last 64 at 0x8000 are VFO A and B), settings 0x9000 (64 bytes), DTMF and ANI 0xA000-0xA1BF. Channels and 0xA000
 * are written in place, through a 4 KB sector buffer (a sector is erased only if a byte in it changed); VFO and
 * settings live in stock's two logs (store/settings.h), so 0x8000 and 0x9000 read and write the current slot.
 * Nothing else is written; the external-flash driver refuses anything past 0xAFFF anyway (calibration at 0xF000).
 *
 * PC test: tests/chirp_host.py runs CHIRP's own driver against this file (PROGRAM_HOST_TEST stubs the hardware).
 */
#include <stdbool.h>
#include "app/program.h"
#include "driver/uart.h"
#include "driver/extflash.h"
#include "store/settings.h"
#ifndef PROGRAM_HOST_TEST
#include "driver/system.h"
#include "driver/st7735s.h"
#include "ui/helper.h"
#include "ui/theme.h"
#define WATCHDOG()  SYSTEM_WatchdogClear()
static void screen(const char *what)
{
	ST7735S_FillRect(0, 0, 128, 128, COLOR_BG);
	UI_PrintString("PROGRAMMING", 31, 52, COLOR_TEXT, COLOR_BG, 1);
	UI_PrintString(what, 40, 68, COLOR_ACCENT, COLOR_BG, 1);
}
#else
void program_test_screen(const char *what);
#define WATCHDOG()  ((void)0)
#define screen(w)   program_test_screen(w)
#endif

#define IDLE_END_MS 3000u
#define SECTOR      0x1000u
#define CH_END      0x8000u          /* channels, written in place */
#define VFO_ADDR    0x8000u          /* 64 bytes: VFO A, B (the VFO log's current slot) */
#define SET_ADDR    0x9000u          /* 64 bytes: settings (the settings log's current slot) */
#define DTMF_ADDR   0xA000u          /* written in place, up to the end of its sector */
#define DTMF_END    0xB000u
#define BAND_ADDR   0xF230u          /* band limits for the F reply (read only) */

static const uint8_t IDENT[16] = { 'P', 'R', 'O', 'G', 'R', 'A', 'M', 'C', 'O', 'L', 'O', 'R', 'P', 'R', 'O', 'U' };
static const uint8_t KEY[4] = { 'C', 'O', ' ', '7' };
#define SEND_LEN 25u                 /* "SEND!" and 20 more bytes */

static uint8_t  s_window[16];
static uint8_t  s_in[4 + 0x80];
static uint32_t s_in_n;
static uint8_t  s_sec[SECTOR];
static int32_t  s_sec_addr = -1;
static bool     s_sec_dirty, s_failed, s_written;

#ifndef PROGRAM_HOST_TEST
/* ------------------------------------------------------------------------------------------- pin clash finder
 * On a radio (2026-11-22) the screen went blank and the external flash read empty once CHIRP's UART was set up in
 * normal mode, although the update mode does the same set-up and its screen works. The flash shares its clock and
 * data pins (PB0, PB1) with the LCD, so its ID is our probe: if it is wrong after UART_Init, leave the routing's bits
 * alone one at a time (then a whole register) until it reads right again, and keep that one off. The result is shown
 * for 3 s at start-up: "PINS OK", "SKIP Rr Bbb" (register r of UART_RouteBits, bit bb), "SKIP Rr ALL", or "PINS ??"
 * (nothing helped: the routing is undone and CHIRP stays off). */
static bool        s_off;
static const char *s_diag = "PINS OK";
static char        s_diag_buf[12];
static uint32_t    s_check_ms;

static bool xf_ok(void)
{
	return EXTFLASH_ReadId() == EXTFLASH_ID;
}

static void diag(int reg, int bit)
{
	char *p = s_diag_buf;
	*p++ = 'S'; *p++ = 'K'; *p++ = 'I'; *p++ = 'P'; *p++ = ' '; *p++ = 'R'; *p++ = (char)('0' + reg); *p++ = ' ';
	if (bit < 0) {
		*p++ = 'A'; *p++ = 'L'; *p++ = 'L';
	} else {
		*p++ = 'B'; *p++ = (char)('0' + bit / 10); *p++ = (char)('0' + bit % 10);
	}
	*p = 0;
	s_diag = s_diag_buf;
}

static void find_clash(void)
{
	if (xf_ok())
		return;
	for (int r = 0; r < UART_ROUTE_REGS; r++)
		for (int b = 0; b < 32; b++) {
			uint32_t m = 1u << b;
			if (!(UART_RouteBits(r) & m))
				continue;
			UART_RouteSkip(r, m);
			if (xf_ok()) {
				diag(r, b);
				return;
			}
			UART_RouteSkip(r, 0);
		}
	for (int r = 0; r < UART_ROUTE_REGS; r++) {
		UART_RouteSkip(r, 0xFFFFFFFFu);
		if (xf_ok()) {
			diag(r, -1);
			return;
		}
		UART_RouteSkip(r, 0);
	}
	for (int r = 0; r < UART_ROUTE_REGS; r++)
		UART_RouteSkip(r, 0xFFFFFFFFu);
	s_off = true;
	s_diag = "PINS ??";
}

/* Screen test (2026-11-22): with CHIRP on the screen stayed blank although the flash ID read right, so the LCD's own
 * lines (CS PB5, reset PB4, D/C PR1) or its state are the suspects. Each step re-initialises the LCD and shows a big
 * label for 2 s; Sean notes which labels he sees:
 *   T1 BOOT     before the cable UART set-up (should always show)
 *   T2 UART     after it, all routing on: if this shows, re-initialising the LCD is the whole fix
 *   T3 R0..R3   one routing register left as it was (0 USB_CON, 1 0x1E5100, 2 0x1E5120, 3 0x1E5124)
 *   T4 NONE     no routing at all (the UART itself on, pins as before)
 * The second line says whether the flash ID reads right at that step. */
static void lcd_step(const char *label)
{
	ST7735S_Init();
	UI_PrintString(label, 4, 40, COLOR_TEXT, COLOR_BG, 2);
	UI_PrintString(xf_ok() ? "FLASH OK" : "FLASH BAD", 4, 72, xf_ok() ? COLOR_ACCENT : COLOR_WARN, COLOR_BG, 1);
	for (int i = 0; i < 20; i++) {
		SYSTEM_DelayMs(100);
		WATCHDOG();
	}
}

void PROGRAM_Init(void)
{
	static const char *const SKIP[UART_ROUTE_REGS] = { "T3 R0", "T3 R1", "T3 R2", "T3 R3" };

#if PROGRAM_SCREEN_TEST
	lcd_step("T1 BOOT");
	UART_Init(PROGRAM_BAUD);
	lcd_step("T2 UART");
	for (int r = 0; r < UART_ROUTE_REGS; r++) {
		UART_RouteSkip(r, 0xFFFFFFFFu);
		lcd_step(SKIP[r]);
		UART_RouteSkip(r, 0);
	}
	for (int r = 0; r < UART_ROUTE_REGS; r++)
		UART_RouteSkip(r, 0xFFFFFFFFu);
	lcd_step("T4 NONE");
	for (int r = 0; r < UART_ROUTE_REGS; r++)
		UART_RouteSkip(r, 0);
#else
	(void)SKIP;
	UART_Init(PROGRAM_BAUD);
#endif
	ST7735S_Init();                       /* as T2: routing on, LCD re-initialised after it */
	find_clash();
	ST7735S_FillRect(0, 0, 128, 128, COLOR_BG);
	UI_PrintString("CHIRP CABLE", 31, 52, COLOR_TEXT, COLOR_BG, 1);
	UI_PrintString(s_diag, 34, 68, s_off ? COLOR_WARN : COLOR_ACCENT, COLOR_BG, 1);
	for (int i = 0; i < 30; i++) {
		SYSTEM_DelayMs(100);
		WATCHDOG();
	}
}
#else
#define s_off false
void PROGRAM_Init(void)
{
	UART_Init(PROGRAM_BAUD);
}
#endif

static void scramble(uint8_t *p, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++) {
		uint8_t k = KEY[i & 3u];
		if (k != ' ' && p[i] != 0 && p[i] != 0xFF && p[i] != k && p[i] != (uint8_t)~k)
			p[i] ^= k;
	}
}

/* ------------------------------------------------------------------------------------------- the sector buffer */
static int flush(void)
{
	int r = 0;

	if (s_sec_addr >= 0 && s_sec_dirty) {
#if SETTINGS_ENABLE_SAVE
		r = EXTFLASH_Erase4K((uint32_t)s_sec_addr);
		if (!r)
			r = EXTFLASH_Program((uint32_t)s_sec_addr, s_sec, SECTOR);
		s_written = true;
#else
		r = -1;                           /* built without saving (features.mk): nothing is written */
#endif
	}
	s_sec_dirty = false;
	if (r)
		s_failed = true;
	return r;
}

#if SETTINGS_ENABLE_SAVE
static int load_sector(uint32_t addr)
{
	uint32_t a = addr & ~(SECTOR - 1u);

	if (s_sec_addr == (int32_t)a)
		return 0;
	if (flush())
		return -1;
	EXTFLASH_Read(a, s_sec, SECTOR);
	s_sec_addr = (int32_t)a;
	return 0;
}
#endif

static bool in_place(uint32_t a, uint32_t n)
{
	return (a + n <= CH_END) || (a >= DTMF_ADDR && a + n <= DTMF_END);
}

/* ------------------------------------------------------------------------------------------- blocks */
static void read_block(uint32_t a, uint8_t *d, uint32_t n)
{
	uint32_t i;

	if (in_place(a, n)) {
		if (s_sec_addr >= 0 && (a & ~(SECTOR - 1u)) == (uint32_t)s_sec_addr && ((a + n - 1u) & ~(SECTOR - 1u)) == (uint32_t)s_sec_addr)
			for (i = 0; i < n; i++)
				d[i] = s_sec[(a & (SECTOR - 1u)) + i];
		else
			EXTFLASH_Read(a, d, n);
		return;
	}
	for (i = 0; i < n; i++)
		d[i] = 0xFF;
	if (a == VFO_ADDR && n <= 64) {
		uint8_t rec[64];
		for (i = 0; i < 64; i++)
			rec[i] = gStock.vfo_raw[i];
		if (gStock.have_vfo) {            /* the VFOs as they are now, not as last saved */
			SETTINGS_EncodeVfo(rec, &gStock.vfo[0]);
			SETTINGS_EncodeVfo(rec + 32, &gStock.vfo[1]);
		}
		for (i = 0; i < n; i++)
			d[i] = rec[i];
	} else if (a == SET_ADDR && n <= sizeof gStock.raw) {
		for (i = 0; i < n; i++)
			d[i] = gStock.raw[i];
	}
}

static bool write_block(uint32_t a, const uint8_t *d, uint32_t n)
{
#if !SETTINGS_ENABLE_SAVE
	(void)a; (void)d; (void)n;
	return false;                         /* built without saving (features.mk): CHIRP's upload stops at once */
#else
	if (in_place(a, n)) {
		for (uint32_t i = 0; i < n; i++) {
			if (load_sector(a + i))
				return false;
			uint8_t *p = &s_sec[(a + i) & (SECTOR - 1u)];
			if (*p != d[i]) {
				*p = d[i];
				s_sec_dirty = true;
			}
		}
		return true;
	}
	if (a == VFO_ADDR && n == 64) {
		s_written = true;
		return SETTINGS_SaveVfoRaw(d) == 0;
	}
	if (a == SET_ADDR && n == sizeof gStock.raw) {
		for (uint32_t i = 0; i < n; i++)
			gStock.raw[i] = d[i];
		gStock.have_settings = true;
		s_written = true;
		return SETTINGS_SaveSettings() == 0;
	}
	return false;                         /* outside what CHIRP writes: refused */
#endif
}

/* ------------------------------------------------------------------------------------------- replies */
static void send(const uint8_t *p, uint32_t n)
{
	UART_Send(p, n);
}

static void ack(bool ok)
{
	uint8_t b = ok ? 0x06 : 0x15;
	send(&b, 1);
}

/* As stock: the 21 bytes at 0xF230 from the second on, leaving out bytes 5, 10 and 15 (mask 0x18420), the first 16. */
static void reply_bands(void)
{
	uint8_t in[21], out[20];
	unsigned r = 1, o = 0;

	EXTFLASH_Read(BAND_ADDR, in, sizeof in);
	while (r <= 20 && o < sizeof out) {
		if (r <= 16 && (0x18420u >> r & 1u))
			r++;
		out[o++] = in[r++];
	}
	send(out, 16);
}

/* How many bytes the command at s_in[0] needs; 0 = not a command (drop the byte). */
static uint32_t needed(void)
{
	switch (s_in[0]) {
	case 'F': case 'M': case 'E':
		return 1;
	case 'S':
		return SEND_LEN;
	case 'R':
		return 4;
	case 'W':
		return s_in_n >= 4 ? 4u + s_in[3] : 4u;
	default:
		return 0;
	}
}

/* Act on one whole command. false = the session is over. */
static bool command(void)
{
	uint8_t out[4 + 0x80];
	uint32_t a = (uint32_t)s_in[1] << 8 | s_in[2], n = s_in[3];

	switch (s_in[0]) {
	case 'F':
		reply_bands();
		break;
	case 'M': {
		/* Stock's first 6 bytes come from RAM 0x198C0+295 (not traced); ours name the radio instead. */
		uint8_t m[15] = { '5', 'R', 'M', 'I', 'N', 'I', ' ', ' ', '+', 'L' };
		EXTFLASH_Read(0xF251u, m + 10, 5);
		send(m, sizeof m);
		break;
	}
	case 'S':
		ack(true);
		break;
	case 'R':
		if (n > 0x80u)
			n = 0x80u;
		out[0] = 'R';
		out[1] = s_in[1];
		out[2] = s_in[2];
		out[3] = (uint8_t)n;
		read_block(a, out + 4, n);
		scramble(out + 4, n);
		send(out, 4 + n);
		break;
	case 'W':
		scramble(s_in + 4, n);
		ack(!s_failed && write_block(a, s_in + 4, n));
		break;
	case 'E':
		return false;
	}
	return true;
}

static int session(void)
{
	uint32_t idle = 0;

	screen("CHIRP");
	s_failed = s_written = s_sec_dirty = false;
	s_sec_addr = -1;
	s_in_n = 0;
	for (;;) {
		uint8_t b[64];
		int got = UART_Receive(b, sizeof b, 50);

		WATCHDOG();
		if (got <= 0) {
			if ((idle += 50u) >= IDLE_END_MS)
				break;
			continue;
		}
		idle = 0;
		for (int i = 0; i < got; i++) {
			uint32_t need;
			s_in[s_in_n++] = b[i];
			need = needed();
			if (!need) {
				s_in_n = 0;              /* not a command: skip it */
				continue;
			}
			if (s_in[0] == 'W' && s_in_n >= 4 && s_in[3] > 0x80u) {
				s_in_n = 0;              /* longer than we take: drop it */
				continue;
			}
			if (s_in_n >= need) {
				s_in_n = 0;
				if (!command())
					goto end;
			}
		}
	}
end:
	flush();
	return s_written ? PROGRAM_DONE_WRITTEN : PROGRAM_DONE_READ;
}

int PROGRAM_Poll(void)
{
	uint8_t b[32];
	int got;

	if (s_off)
		return PROGRAM_IDLE;
#ifndef PROGRAM_HOST_TEST
	if (++s_check_ms >= 200u) {          /* every 200 polls: has something else broken the flash since start-up? */
		s_check_ms = 0;
		if (!xf_ok()) {
			find_clash();
			screen(s_diag);              /* "PROGRAMMING" + the result; the caller redraws the main screen */
			for (int i = 0; i < 30; i++) {
				SYSTEM_DelayMs(100);
				WATCHDOG();
			}
			return PROGRAM_DONE_READ;
		}
	}
#endif
	got = UART_Receive(b, sizeof b, 0);

	for (int i = 0; i < got; i++) {
		for (int j = 0; j < 15; j++)
			s_window[j] = s_window[j + 1];
		s_window[15] = b[i];
		bool match = true;
		for (int j = 0; j < 16; j++)
			if (s_window[j] != IDENT[j])
				match = false;
		if (match) {
			for (int j = 0; j < 16; j++)
				s_window[j] = 0;
			ack(true);
			return session();
		}
	}
	return PROGRAM_IDLE;
}
