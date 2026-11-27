/* Open-UV5R-Mini: AX.25 UI frames and HDLC bits. See radio/ax25.h. */
#include "radio/ax25.h"

uint16_t AX25_Fcs(const uint8_t *p, unsigned n)
{
	uint16_t crc = 0xFFFFu;

	while (n--) {
		crc ^= *p++;
		for (int k = 0; k < 8; k++)
			crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0x8408u) : (uint16_t)(crc >> 1);
	}
	return (uint16_t)~crc;
}

static bool call_char(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

static char upper(char c)
{
	return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}

/* one call sign from s up to a ',' or the end; returns the characters used, 0 if not a call sign */
static unsigned parse_one(const char *s, char call[7], uint8_t *ssid)
{
	unsigned i = 0, n = 0, v = 0;

	while (s[i] && s[i] != ',' && s[i] != '-') {
		char c = upper(s[i]);
		if (!call_char(c) || n >= 6)
			return 0;
		call[n++] = c;
		i++;
	}
	if (!n)
		return 0;
	call[n] = 0;
	*ssid = 0;
	if (s[i] == '-') {
		unsigned d = 0;
		i++;
		while (s[i] >= '0' && s[i] <= '9' && d < 2) {
			v = v * 10u + (unsigned)(s[i] - '0');
			i++;
			d++;
		}
		if (!d || v > 15u)
			return 0;
		*ssid = (uint8_t)v;
	}
	if (s[i] && s[i] != ',')
		return 0;
	return i;
}

bool AX25_ParseCall(const char *s, char call[7], uint8_t *ssid)
{
	unsigned n = parse_one(s, call, ssid);
	return n && !s[n];
}

static void put_addr(uint8_t *o, const char *call, uint8_t ssid, bool last)
{
	for (unsigned k = 0; k < 6; k++) {
		char c = *call ? *call++ : ' ';
		o[k] = (uint8_t)(c << 1);
	}
	o[6] = (uint8_t)(0x60u | (ssid & 15u) << 1 | (last ? 1u : 0u));   /* reserved bits 5-6 set */
}

unsigned AX25_BuildUi(uint8_t *out, unsigned max, const char *dst, const char *src, const char *path,
                      const uint8_t *info, unsigned info_len)
{
	char call[7];
	uint8_t ssid;
	unsigned n = 0, used;
	bool no_path = !path || !*path;

	if (max < 14u + 2u + info_len + 2u)
		return 0;
	if (!AX25_ParseCall(dst, call, &ssid))
		return 0;
	put_addr(out, call, ssid, false);
	out[6] |= 0x80u;                      /* command frame: C bit in the destination (AX.25 2.x) */
	if (!AX25_ParseCall(src, call, &ssid))
		return 0;
	put_addr(out + 7, call, ssid, no_path);
	n = 14;
	while (!no_path) {
		if (n + 7u + 2u + info_len + 2u > max || n >= 70u)
			return 0;
		used = parse_one(path, call, &ssid);
		if (!used)
			return 0;
		path += used;
		if (*path == ',')
			path++;
		put_addr(out + n, call, ssid, !*path);
		n += 7;
		no_path = !*path;
	}
	out[n++] = 0x03;                      /* UI */
	out[n++] = 0xF0;                      /* no layer 3 */
	for (unsigned k = 0; k < info_len; k++)
		out[n++] = info[k];
	{
		uint16_t f = AX25_Fcs(out, n);
		out[n++] = (uint8_t)f;
		out[n++] = (uint8_t)(f >> 8);
	}
	return n;
}

/* one address field as "CALL-SSID"; returns the end */
static char *addr_text(char *o, char *end, const uint8_t *a)
{
	for (unsigned k = 0; k < 6 && o < end; k++) {
		char c = (char)(a[k] >> 1);
		if (c != ' ')
			*o++ = c;
	}
	if (a[6] >> 1 & 15u) {
		unsigned s = a[6] >> 1 & 15u;
		if (o < end) *o++ = '-';
		if (s >= 10u && o < end) *o++ = '1';
		if (o < end) *o++ = (char)('0' + s % 10u);
	}
	return o;
}

static bool addr_ok(const uint8_t *a)
{
	for (unsigned k = 0; k < 6; k++) {
		char c = (char)(a[k] >> 1);
		if ((a[k] & 1u) || !(call_char(c) || c == ' '))
			return false;
	}
	return (char)(a[0] >> 1) != ' ';
}

unsigned AX25_ToText(const uint8_t *f, unsigned len, char *out, unsigned max)
{
	unsigned na = 0, i;
	char *o = out, *end = out + max - 1;
	int last_h = -1;

	if (max < 2u)
		return 0;
	while ((na + 1u) * 7u <= len) {       /* the address fields, up to the one with bit 0 set */
		if (!addr_ok(f + na * 7u))
			return 0;
		na++;
		if (f[na * 7u - 1u] & 1u)
			break;
	}
	if (na < 2u || na > 10u || !(f[na * 7u - 1u] & 1u) || na * 7u + 1u > len)
		return 0;
	for (i = 2; i < na; i++)              /* the '*' goes after the last digipeater that has repeated it */
		if (f[i * 7u + 6u] & 0x80u)
			last_h = (int)i;
	o = addr_text(o, end, f + 7);         /* source > destination, path */
	if (o < end) *o++ = '>';
	o = addr_text(o, end, f);
	for (i = 2; i < na; i++) {
		if (o < end) *o++ = ',';
		o = addr_text(o, end, f + i * 7u);
		if ((int)i == last_h && o < end)
			*o++ = '*';
	}
	if (o < end) *o++ = ':';
	i = na * 7u + 1u;                     /* past the control byte */
	if ((f[na * 7u] & 0xEFu) == 0x03u && i < len)
		i++;                              /* UI: the PID byte */
	for (; i < len && o < end; i++) {
		char c = (char)f[i];
		*o++ = (c >= 32 && c < 127) ? c : '.';
	}
	*o = 0;
	return (unsigned)(o - out);
}

/* ------------------------------------------------------------------ sending */
void AX25_TxStart(Ax25Tx_t *t, const uint8_t *frame, unsigned len, unsigned pre, unsigned post)
{
	t->p = frame;
	t->n = (uint16_t)len;
	t->i = 0;
	t->pre = (uint16_t)(pre ? pre : 1u);
	t->post = (uint16_t)(post ? post : 1u);
	t->bit = 8;
	t->ones = 0;
	t->flag = t->stuff = t->done = false;
	t->level = true;
}

int AX25_TxNext(Ax25Tx_t *t)
{
	int b;

	if (t->stuff) {
		t->stuff = false;
		t->ones = 0;
		b = 0;
	} else {
		if (t->bit >= 8u) {
			if (t->pre) {
				t->pre--;
				t->byte = 0x7E;
				t->flag = true;
			} else if (t->i < t->n) {
				t->byte = t->p[t->i++];
				t->flag = false;
			} else if (t->post) {
				t->post--;
				t->byte = 0x7E;
				t->flag = true;
			} else {
				t->done = true;
				return -1;
			}
			t->bit = 0;
		}
		b = t->byte >> t->bit++ & 1;
		if (t->flag)
			t->ones = 0;
		else if (b) {
			if (++t->ones == 5u)
				t->stuff = true;
		} else
			t->ones = 0;
	}
	if (!b)
		t->level = !t->level;
	return t->level ? 1 : 0;
}

/* ------------------------------------------------------------------ receiving */
void AX25_RxInit(Ax25Rx_t *r)
{
	r->n = 0;
	r->sr = 0;
	r->byte = r->bits = r->ones = 0;
	r->last = false;
	r->in = false;
}

unsigned AX25_RxBit(Ax25Rx_t *r, bool level)
{
	uint8_t bit = level == r->last;       /* NRZI: no change is a 1 */

	r->last = level;
	r->sr = (uint8_t)(r->sr >> 1 | bit << 7);
	if (r->sr == 0x7Eu) {                 /* a flag ends one frame and may start the next */
		unsigned got = 0;
		if (r->in && r->n >= AX25_MIN_FRAME && r->bits == 7u) {
			if (AX25_Fcs(r->buf, r->n - 2u) == (uint16_t)(r->buf[r->n - 2u] | r->buf[r->n - 1u] << 8)) {
				got = r->n - 2u;
				r->frames++;
			} else
				r->bad_fcs++;
		}
		r->in = true;
		r->n = 0;
		r->bits = r->ones = 0;
		return got;
	}
	if (!r->in)
		return 0;
	if (bit) {
		if (++r->ones >= 7u) {            /* seven 1s: abort, or the line went idle */
			r->in = false;
			return 0;
		}
	} else {
		if (r->ones == 5u) {              /* the stuffed 0 */
			r->ones = 0;
			return 0;
		}
		r->ones = 0;
	}
	r->byte = (uint8_t)(r->byte >> 1 | bit << 7);
	if (++r->bits == 8u) {
		r->bits = 0;
		if (r->n >= AX25_MAX_FRAME) {
			r->in = false;
			return 0;
		}
		r->buf[r->n++] = r->byte;
	}
	return 0;
}
