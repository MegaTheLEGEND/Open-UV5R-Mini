/* Open-UV5R-Mini: the remote-control monitor on the cable. See app/monitor.h. */
#include "app/monitor.h"
#if DEBUGGING
#include "app/version.h"
#include "driver/bk4819.h"
#include "driver/uart.h"

bool (*gMonApp)(const char *args);

static char    s_line[120];
static uint8_t s_n;
static bool    s_events = true;

/* the LCD text shadow: 21 columns of 6 px, 32 rows of 4 px */
#define SH_COLS 21u
#define SH_ROWS 32u
static char s_shadow[SH_ROWS][SH_COLS];

void MON_Char(char c, uint8_t x, uint8_t y, uint8_t scale)
{
	unsigned col = x / 6u, row = (y + 2u) / 4u;
	if (c >= 'a' && c <= 'z')
		c = (char)(c - 32);
	if (col < SH_COLS && row < SH_ROWS) {
		s_shadow[row][col] = c == ' ' ? 0 : c;
		for (unsigned k = 1; k < scale && col + k < SH_COLS; k++)
			s_shadow[row][col + k] = 0;
	}
}

void MON_Clear(uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
	unsigned c0 = (x + 3u) / 6u, c1 = (x + w + 3u) / 6u, r0 = (y + 2u) / 4u, r1 = (y + h + 2u) / 4u;
	for (unsigned r = r0; r < r1 && r < SH_ROWS; r++)
		for (unsigned c = c0; c < c1 && c < SH_COLS; c++)
			s_shadow[r][c] = 0;
}

void MON_Line(const char *s)
{
	unsigned n = 0;
	while (s[n])
		n++;
	UART_Send((const uint8_t *)s, n);
	UART_Send((const uint8_t *)"\r\n", 2);
}

void MON_Event(const char *kind, const char *text)
{
	char b[200];
	unsigned n = 0;
	if (!s_events)
		return;
	b[n++] = '#';
	while (*kind && n < 20u)
		b[n++] = *kind++;
	b[n++] = ' ';
	while (*text && n < sizeof b - 1u)
		b[n++] = *text++;
	b[n] = 0;
	MON_Line(b);
}

/* ------------------------------------------------------------------ parsing and printing */
static const char *skip(const char *s) { while (*s == ' ') s++; return s; }

static bool hex(const char **s, uint32_t *v)
{
	const char *p = skip(*s);
	uint32_t x = 0;
	unsigned n = 0;
	if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
		p += 2;
	for (;; p++, n++) {
		char c = *p;
		if (c >= '0' && c <= '9') x = x << 4 | (uint32_t)(c - '0');
		else if (c >= 'a' && c <= 'f') x = x << 4 | (uint32_t)(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F') x = x << 4 | (uint32_t)(c - 'A' + 10);
		else break;
	}
	*s = p;
	*v = x;
	return n > 0;
}

static bool dec(const char **s, uint32_t *v)
{
	const char *p = skip(*s);
	uint32_t x = 0;
	unsigned n = 0;
	for (; *p >= '0' && *p <= '9'; p++, n++)
		x = x * 10u + (uint32_t)(*p - '0');
	*s = p;
	*v = x;
	return n > 0;
}

static char *put(char *o, const char *s) { while (*s) *o++ = *s++; *o = 0; return o; }

static char *phex(char *o, uint32_t v, unsigned digits)
{
	for (unsigned k = digits; k; k--) {
		unsigned d = v >> (4u * (k - 1u)) & 15u;
		*o++ = (char)(d < 10u ? '0' + d : 'A' + d - 10u);
	}
	*o = 0;
	return o;
}

/* "word" == s up to a space or the end */
static bool is(const char *s, const char *word)
{
	while (*word)
		if (*s++ != *word++)
			return false;
	return *s == 0 || *s == ' ';
}

static const char *after(const char *s) { while (*s && *s != ' ') s++; return skip(s); }

static const struct { const char *name; KEY_Code_t k; } KEYS[] = {
	{ "0", KEY_0 }, { "1", KEY_1 }, { "2", KEY_2 }, { "3", KEY_3 }, { "4", KEY_4 }, { "5", KEY_5 }, { "6", KEY_6 },
	{ "7", KEY_7 }, { "8", KEY_8 }, { "9", KEY_9 }, { "*", KEY_STAR }, { "#", KEY_HASH }, { "MENU", KEY_MENU },
	{ "UP", KEY_UP }, { "DOWN", KEY_DOWN }, { "EXIT", KEY_EXIT }, { "VM", KEY_VM }, { "AB", KEY_AB },
	{ "LAMP", KEY_LAMP }, { "EQ", KEY_EQUALS },
};

/* ------------------------------------------------------------------ commands */
static bool run(const char *s)
{
	char b[128], *o;
	uint32_t a, v, n;

	if (is(s, "ping")) {
		put(put(b, "@pong "), VERSION_Build());
		MON_Line(b);
		return true;
	}
	if (is(s, "state")) {
		o = put(b, "@");
		APP_MonState(o, sizeof b - 1u);
		MON_Line(b);
		return true;
	}
	if (is(s, "screen")) {
		for (unsigned r = 0; r < SH_ROWS; r++) {
			bool any = false;
			for (unsigned c = 0; c < SH_COLS; c++)
				any = any || s_shadow[r][c];
			if (!any)
				continue;
			o = phex(put(b, "@"), r * 4u, 2);       /* the pixel row, hex */
			*o++ = '|';
			for (unsigned c = 0; c < SH_COLS; c++)
				*o++ = s_shadow[r][c] ? s_shadow[r][c] : ' ';
			*o++ = '|';
			*o = 0;
			MON_Line(b);
		}
		return true;
	}
	if (is(s, "events")) {
		s = after(s);
		s_events = *s != '0';
		return true;
	}
	if (is(s, "r") || is(s, "rb")) {
		bool bytes = s[1] == 'b';
		s = after(s);
		if (!hex(&s, &a))
			return false;
		if (!hex(&s, &n) || !n)
			n = bytes ? 16u : 1u;
		if (n > (bytes ? 256u : 64u))
			n = bytes ? 256u : 64u;
		for (uint32_t i = 0; i < n; ) {
			o = phex(put(b, "@"), bytes ? a + i : a + 4u * i, 8);
			*o++ = ':';
			for (unsigned k = 0; k < (bytes ? 16u : 4u) && i < n; k++, i++) {
				*o++ = ' ';
				if (bytes)
					o = phex(o, *(volatile uint8_t *)(uintptr_t)(a + i), 2);
				else
					o = phex(o, *(volatile uint32_t *)(uintptr_t)((a & ~3u) + 4u * i), 8);
			}
			MON_Line(b);
		}
		return true;
	}
	if (is(s, "w")) {
		s = after(s);
		if (!hex(&s, &a) || !hex(&s, &v))
			return false;
		*(volatile uint32_t *)(uintptr_t)(a & ~3u) = v;
		return true;
	}
	if (is(s, "bk")) {
		s = after(s);
		if (!hex(&s, &a) || a > 0x7Fu)
			return false;
		if (hex(&s, &v))
			BK4819_WriteRegister((uint8_t)a, (uint16_t)v);
		o = phex(put(b, "@bk "), a, 2);
		*o++ = '=';
		phex(o, BK4819_ReadRegister((uint8_t)a), 4);
		MON_Line(b);
		return true;
	}
	if (is(s, "bkdump")) {
		for (unsigned r = 0; r < 0x80u; r += 8u) {
			o = phex(put(b, "@"), r, 2);
			*o++ = ':';
			for (unsigned k = 0; k < 8u; k++) {
				*o++ = ' ';
				o = phex(o, BK4819_ReadRegister((uint8_t)(r + k)), 4);
			}
			MON_Line(b);
		}
		return true;
	}
	if (is(s, "key")) {
		s = after(s);
		for (unsigned i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++)
			if (is(s, KEYS[i].name)) {
				const char *t = after(s);
				if (!dec(&t, &n) || !n)
					n = 120u;
				APP_MonKey(KEYS[i].k, (uint16_t)(n > 10000u ? 10000u : n));
				return true;
			}
		return false;
	}
	if (is(s, "app")) {
		return gMonApp && gMonApp(after(s));   /* no app listening: @err */
	}
	return false;
}

void MON_Feed(const uint8_t *in, int got)
{
	for (int i = 0; i < got; i++) {
		char c = (char)in[i];
		if (c == '\r' || c == '\n') {
			s_line[s_n] = 0;
			if (s_n && s_line[0] == '!') {
				const char *l = skip(s_line + 1);
				bool ok = run(l);
				MON_Line(ok ? "@ok" : "@err ? (ping state screen r w rb bk bkdump key app events)");
			}
			s_n = 0;
		} else if (c >= 32 && c < 127) {
			if (s_n < sizeof s_line - 1u)
				s_line[s_n++] = c;
		} else
			s_n = 0;                     /* binary (CHIRP): not a command line */
	}
}
#endif
