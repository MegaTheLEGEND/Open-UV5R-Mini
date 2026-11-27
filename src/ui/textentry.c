/* Open-UV5R-Mini: keypad typing for every text field (ui/textentry.h). Our own code. */
#include "ui/textentry.h"

bool gTextActive;

/* the characters on each key in letters mode, most used first; the digit last */
static const char *const TAPS[10] = {
	" 0", ".-/,?!'+#*()1", "ABC2", "DEF3", "GHI4", "JKL5", "MNO6", "PQRS7", "TUV8", "WXYZ9"
};

/* every character a field allows, in the order UP/DOWN goes through them */
static const char ALL_FREE[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,-/?!'+#*()=:;@&%\"<>$_";
static const char ALL_NAME[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-./+#*()";
static const char ALL_CALL[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";

static const char *allowed(const TextEntry_t *t)
{
	return t->kind == TEXT_CALL ? ALL_CALL : t->kind == TEXT_NAME ? ALL_NAME : ALL_FREE;
}

static bool ok(const TextEntry_t *t, char c)
{
	for (const char *a = allowed(t); *a; a++)
		if (*a == c)
			return true;
	return false;
}

/* the i-th character on digit d's key that the field allows (wrapping round); 0 if none */
static char tap_char(const TextEntry_t *t, int d, unsigned i)
{
	unsigned n = 0;
	const char *g = TAPS[d];

	for (const char *p = g; *p; p++)
		n += ok(t, *p);
	if (!n)
		return 0;
	i %= n;
	for (const char *p = g; *p; p++)
		if (ok(t, *p) && !i--)
			return *p;
	return 0;
}

static int digit(KEY_Code_t k)
{
	switch (k) {
	case KEY_0: return 0;
	case KEY_1: return 1;
	case KEY_2: return 2;
	case KEY_3: return 3;
	case KEY_4: return 4;
	case KEY_5: return 5;
	case KEY_6: return 6;
	case KEY_7: return 7;
	case KEY_8: return 8;
	case KEY_9: return 9;
	default:    return -1;
	}
}

void TEXT_Start(TextEntry_t *t, char *buf, uint8_t max, TextKind_t kind)
{
	t->buf = buf;
	t->max = max;
	t->kind = (uint8_t)kind;
	t->len = 0;
	while (t->len < max && buf[t->len])
		t->len++;
	buf[t->len] = 0;
	t->numbers = false;
	t->tap_key = -1;
	gTextActive = true;
}

void TEXT_End(void) { gTextActive = false; }

static void put(TextEntry_t *t, char c)
{
	if (c && t->len < t->max) {
		t->buf[t->len++] = c;
		t->buf[t->len] = 0;
	}
}

TextResult_t TEXT_Key(TextEntry_t *t, KEY_Code_t k, uint32_t now_ms)
{
	int d = digit(k);

	if (k == KEY_MENU || k == KEY_EXIT) {
		TEXT_End();
		return k == KEY_MENU ? TEXT_DONE : TEXT_CANCEL;
	}
	if (k == KEY_HASH) {                 /* F: letters <-> numbers */
		t->numbers = !t->numbers;
		t->tap_key = -1;
	} else if (k == KEY_STAR) {
		if (t->len)
			t->buf[--t->len] = 0;
		t->tap_key = -1;
	} else if (k == KEY_UP || k == KEY_DOWN) {
		const char *a = allowed(t);
		int n = 0, i = 0;
		while (a[n])
			n++;
		if (!t->len) {
			put(t, a[k == KEY_UP ? 0 : n - 1]);
		} else {
			while (i < n && a[i] != t->buf[t->len - 1])
				i++;
			t->buf[t->len - 1] = a[((i < n ? i : 0) + n + (k == KEY_UP ? 1 : -1)) % n];
		}
		t->tap_key = -1;
	} else if (d >= 0 && t->numbers) {
		if (ok(t, (char)('0' + d)))
			put(t, (char)('0' + d));
		t->tap_key = -1;
	} else if (d >= 0) {
		if (d == t->tap_key && now_ms - t->tap_ms < TEXT_TAP_MS && t->len) {
			t->tap_i++;
			t->buf[t->len - 1] = tap_char(t, d, t->tap_i);
		} else {
			t->tap_i = 0;
			if (t->len >= t->max || !tap_char(t, d, 0)) {
				t->tap_key = -1;
				return TEXT_EDITING;
			}
			put(t, tap_char(t, d, 0));
		}
		t->tap_key = (int8_t)d;
		t->tap_ms = now_ms;
	}
	return TEXT_EDITING;
}

const char *TEXT_ModeName(const TextEntry_t *t) { return t->numbers ? "123" : "ABC"; }

void TEXT_Line(const TextEntry_t *t, char *out, uint8_t cols)
{
	uint8_t show = t->len < t->max && cols ? (uint8_t)(cols - 1u) : cols;
	uint8_t from = t->len > show ? (uint8_t)(t->len - show) : 0;
	uint8_t n = 0;

	for (uint8_t i = from; i < t->len; i++)
		out[n++] = t->buf[i];
	if (t->len < t->max && n < cols)
		out[n++] = '_';
	out[n] = 0;
}
