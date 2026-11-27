/* Open-UV5R-Mini: APRS position reports out, heard packets described. See radio/aprs.h. */
#include "radio/aprs.h"
#include "radio/ax25.h"

static const struct { char table, code; const char *name; } ICONS[APRS_ICONS] = {
	{ '/', '[', "PERSON" }, { '/', '>', "CAR" }, { '/', '-', "HOUSE" }, { '/', 'b', "BIKE" },
	{ '/', 'k', "TRUCK" },  { '/', 'R', "RV" },  { '/', 'Y', "BOAT" },  { '/', 'j', "JEEP" },
};

const char *APRS_IconName(uint8_t i)
{
	return ICONS[i % APRS_ICONS].name;
}

/* ------------------------------------------------------------------ settings in 10 bytes (80 bits)
 * call 6 x 6 bits (0 space, 1-26 A-Z, 27-36 0-9), SSID 4, latitude in 1/10 minute + 54000 (17 bits),
 * longitude + 108000 (18), icon 3, beacon every 2 */
static void put_bits(uint8_t *o, unsigned *at, uint32_t v, unsigned n)
{
	for (unsigned k = 0; k < n; k++, (*at)++)
		if (v >> k & 1u)
			o[*at >> 3] |= (uint8_t)(1u << (*at & 7u));
}

static uint32_t get_bits(const uint8_t *in, unsigned *at, unsigned n)
{
	uint32_t v = 0;
	for (unsigned k = 0; k < n; k++, (*at)++)
		if (in[*at >> 3] >> (*at & 7u) & 1u)
			v |= 1u << k;
	return v;
}

static int32_t clamp(int32_t v, int32_t lim)
{
	return v > lim ? lim : v < -lim ? -lim : v;
}

static int32_t div10(int32_t v)          /* rounded, either sign */
{
	return v >= 0 ? (v + 5) / 10 : -((-v + 5) / 10);
}

void APRS_Pack(const AprsConf_t *c, uint8_t out[APRS_PACK_LEN])
{
	unsigned at = 0;

	for (unsigned i = 0; i < APRS_PACK_LEN; i++)
		out[i] = 0;
	for (unsigned i = 0; i < 6; i++) {
		char ch = c->call[i];
		uint32_t v = 0;
		if (ch >= 'A' && ch <= 'Z')
			v = (uint32_t)(ch - 'A' + 1);
		else if (ch >= '0' && ch <= '9')
			v = (uint32_t)(ch - '0' + 27);
		if (!ch)
			for (; i < 6; i++)
				put_bits(out, &at, 0, 6);
		else
			put_bits(out, &at, v, 6);
		if (!ch)
			break;
	}
	put_bits(out, &at, c->ssid & 15u, 4);
	put_bits(out, &at, (uint32_t)(div10(clamp(c->lat, APRS_LAT_MAX)) + 54000), 17);
	put_bits(out, &at, (uint32_t)(div10(clamp(c->lon, APRS_LON_MAX)) + 108000), 18);
	put_bits(out, &at, c->icon % APRS_ICONS, 3);
	put_bits(out, &at, c->every & 3u, 2);
}

void APRS_Unpack(const uint8_t in[APRS_PACK_LEN], AprsConf_t *c)
{
	unsigned at = 0, n = 0;

	for (unsigned i = 0; i < 6; i++) {
		uint32_t v = get_bits(in, &at, 6);
		if (v >= 1u && v <= 26u)
			c->call[n++] = (char)('A' + v - 1u);
		else if (v >= 27u && v <= 36u)
			c->call[n++] = (char)('0' + v - 27u);
	}
	c->call[n] = 0;
	c->ssid = (uint8_t)get_bits(in, &at, 4);
	c->lat = clamp(((int32_t)get_bits(in, &at, 17) - 54000) * 10, APRS_LAT_MAX);
	c->lon = clamp(((int32_t)get_bits(in, &at, 18) - 108000) * 10, APRS_LON_MAX);
	c->icon = (uint8_t)get_bits(in, &at, 3);
	c->every = (uint8_t)get_bits(in, &at, 2);
	if (!n)                               /* all bytes 0 (never set): no position either */
		c->lat = c->lon = 0;
}

static char *put_num(char *b, uint32_t v, unsigned digits)
{
	for (unsigned k = digits; k; k--) {
		b[k - 1] = (char)('0' + v % 10u);
		v /= 10u;
	}
	return b + digits;
}

char *APRS_CallText(char *out, const AprsConf_t *c)
{
	char *o = out;
	for (const char *p = c->call; *p; )
		*o++ = *p++;
	if (c->ssid) {
		*o++ = '-';
		if (c->ssid >= 10u)
			*o++ = '1';
		*o++ = (char)('0' + c->ssid % 10u);
	}
	*o = 0;
	return o;
}

/* DDMM.mm or DDDMM.mm and the hemisphere letter */
static char *angle_text(char *b, int32_t v, unsigned deg_digits, char pos, char neg)
{
	uint32_t a = (uint32_t)(v < 0 ? -v : v);
	b = put_num(b, a / 6000u, deg_digits);
	b = put_num(b, a / 100u % 60u, 2);
	*b++ = '.';
	b = put_num(b, a % 100u, 2);
	*b++ = v < 0 ? neg : pos;
	*b = 0;
	return b;
}

char *APRS_LatText(char *b, int32_t lat) { return angle_text(b, lat, 2, 'N', 'S'); }
char *APRS_LonText(char *b, int32_t lon) { return angle_text(b, lon, 3, 'E', 'W'); }

unsigned APRS_PositionInfo(const AprsConf_t *c, char *out, unsigned max)
{
	char *o = out;
	const char *cm = APRS_COMMENT;

	if (max < 21u + sizeof APRS_COMMENT)
		return 0;
	*o++ = '!';                           /* position, no time, no messaging */
	o = APRS_LatText(o, c->lat);
	*o++ = ICONS[c->icon % APRS_ICONS].table;
	o = APRS_LonText(o, c->lon);
	*o++ = ICONS[c->icon % APRS_ICONS].code;
	while (*cm)
		*o++ = *cm++;
	*o = 0;
	return (unsigned)(o - out);
}

unsigned APRS_BuildBeacon(const AprsConf_t *c, uint8_t *frame, unsigned max)
{
	char info[64], src[10];
	unsigned n;

	if (!c->call[0])
		return 0;
	n = APRS_PositionInfo(c, info, sizeof info);
	APRS_CallText(src, c);
	return AX25_BuildUi(frame, max, APRS_TOCALL, src, APRS_PATH, (const uint8_t *)info, n);
}

/* ------------------------------------------------------------------ heard packets */
static void copy(char *d, const char *s, unsigned n)
{
	while (n-- > 1 && *s)
		*d++ = *s++;
	*d = 0;
}

static bool digit(char c) { return c >= '0' && c <= '9'; }

/* "DDMM.mmN" (spaces of position ambiguity count as 0) -> 1/100 minute; false if not that */
static bool parse_angle(const char *s, unsigned deg_digits, char pos, char neg, int32_t *out)
{
	uint32_t v = 0;
	unsigned n = deg_digits + 2u;

	for (unsigned i = 0; i < n + 3u; i++) {
		char c = s[i];
		if (i == n) {
			if (c != '.')
				return false;
			continue;
		}
		if (c == ' ')
			c = '0';
		if (!digit(c))
			return false;
		v = v * 10u + (uint32_t)(c - '0');
	}
	/* v = degrees, minutes, hundredths as decimal digits */
	{
		uint32_t deg = v / 10000u, rest = v % 10000u;
		if (rest >= 6000u)
			return false;
		v = deg * 6000u + rest;
	}
	if (s[n + 3u] == pos)
		*out = (int32_t)v;
	else if (s[n + 3u] == neg)
		*out = -(int32_t)v;
	else
		return false;
	return true;
}

static bool b91(const char *s, uint32_t *out)
{
	uint32_t v = 0;
	for (int i = 0; i < 4; i++) {
		if (s[i] < 33 || s[i] > 124)
			return false;
		v = v * 91u + (uint32_t)(s[i] - 33);
	}
	*out = v;
	return true;
}

/* a position after its type character: plain or compressed; returns the comment that follows, or 0 */
static const char *parse_pos(const char *p, AprsHeard_t *h)
{
	uint32_t y, x;

	if (digit(p[0]) || p[0] == ' ') {     /* plain: DDMM.mmN T DDDMM.mmW C */
		if (!parse_angle(p, 2, 'N', 'S', &h->lat) || !p[8] || !parse_angle(p + 9, 3, 'E', 'W', &h->lon) || !p[18])
			return 0;
		h->sym_table = p[8];
		h->sym = p[18];
		h->has_pos = true;
		return p + 19;
	}
	if (b91(p + 1, &y) && b91(p + 5, &x) && p[9]) {   /* compressed: T YYYY XXXX C cs T */
		/* latitude 90 - y / 380926 degrees, longitude -180 + x / 190463 degrees; in 1/100 minute */
		h->lat = 540000 - (int32_t)(y / 380926u * 6000u + y % 380926u * 6000u / 380926u);
		h->lon = -1080000 + (int32_t)(x / 190463u * 6000u + x % 190463u * 6000u / 190463u);
		h->sym_table = p[0];
		h->sym = p[9];
		h->has_pos = true;
		{
			const char *c = p + 10;
			for (int k = 0; k < 3 && *c; k++)
				c++;
			return c;
		}
	}
	return 0;
}

/* Mic-E: the latitude is in the destination address, the longitude in the information (APRS 1.01 chapter 10) */
static void parse_mice(const char *dst, const char *p, AprsHeard_t *h)
{
	uint32_t v = 0;
	bool north, west, plus100;
	int deg, min, hun;

	for (int i = 0; i < 6; i++) {
		char c = dst[i];
		int d;
		if (c >= '0' && c <= '9') d = c - '0';
		else if (c >= 'A' && c <= 'J') d = c - 'A';
		else if (c >= 'P' && c <= 'Y') d = c - 'P';
		else if (c == 'K' || c == 'L' || c == 'Z') d = 0;
		else return;
		v = v * 10u + (uint32_t)d;
	}
	north = dst[3] >= 'P' && dst[3] <= 'Z';
	plus100 = dst[4] >= 'P' && dst[4] <= 'Z';
	west = dst[5] >= 'P' && dst[5] <= 'Z';
	if (!p[0] || !p[1] || !p[2] || !p[3] || !p[4] || !p[5] || !p[6] || !p[7])
		return;
	deg = p[0] - 28 + (plus100 ? 100 : 0);
	if (deg >= 180 && deg <= 189) deg -= 80;
	else if (deg >= 190 && deg <= 199) deg -= 190;
	min = p[1] - 28;
	if (min >= 60) min -= 60;
	hun = p[2] - 28;
	if (deg < 0 || deg > 179 || min < 0 || min > 59 || hun < 0 || hun > 99 || v / 10000u > 89u)
		return;
	h->lat = (int32_t)(v / 10000u * 6000u + v % 10000u);
	if (!north)
		h->lat = -h->lat;
	h->lon = deg * 6000 + min * 100 + hun;
	if (west)
		h->lon = -h->lon;
	h->sym = p[6];
	h->sym_table = p[7];
	h->has_pos = true;
	copy(h->text, p + 8, sizeof h->text);
}

bool APRS_Describe(const char *t, const char *me, AprsHeard_t *h)
{
	const char *gt = t, *info, *k;
	char dst[7];
	unsigned n = 0;

	h->from[0] = h->kind[0] = h->text[0] = 0;
	h->has_pos = false;
	h->lat = h->lon = 0;
	h->sym_table = h->sym = 0;
	while (*gt && *gt != '>')
		gt++;
	if (*gt != '>' || gt == t)
		return false;
	for (const char *p = t; p < gt && n < sizeof h->from - 1u; p++)
		h->from[n++] = *p;
	h->from[n] = 0;
	for (n = 0, k = gt + 1; *k && *k != ',' && *k != ':' && *k != '-' && n < 6u; k++)
		dst[n++] = *k;
	dst[n] = 0;
	for (info = gt; *info && *info != ':'; info++)
		;
	if (!*info)
		return false;
	info++;

	switch (info[0]) {
	case '!': case '=':
		copy(h->kind, "POS", sizeof h->kind);
		k = parse_pos(info + 1, h);
		copy(h->text, k ? k : info + 1, sizeof h->text);
		break;
	case '/': case '@':                   /* with a time stamp first (7 characters) */
		copy(h->kind, "POS", sizeof h->kind);
		k = info[1] && info[7] ? parse_pos(info + 8, h) : 0;
		copy(h->text, k ? k : info + 1, sizeof h->text);
		break;
	case '`': case '\'': case 0x1C: case 0x1D:
		copy(h->kind, "MIC-E", sizeof h->kind);
		if (n == 6u)
			parse_mice(dst, info + 1, h);
		break;
	case ':': {                           /* :ADDRESSEE:text */
		char to[10];
		unsigned m = 0;
		for (k = info + 1; *k && *k != ':' && m < 9u; k++)
			if (*k != ' ')
				to[m++] = *k;
		to[m] = 0;
		{
			bool mine = me && *me;
			const char *a = to, *b = me;
			while (mine && (*a || *b))
				if (*a++ != *b++)
					mine = false;
			copy(h->kind, mine ? "MSG ME" : "MSG", sizeof h->kind);
		}
		copy(h->text, *k == ':' ? k + 1 : info + 1, sizeof h->text);
		break;
	}
	case '>': copy(h->kind, "STATUS", sizeof h->kind); copy(h->text, info + 1, sizeof h->text); break;
	case ';': copy(h->kind, "OBJECT", sizeof h->kind); copy(h->text, info + 1, sizeof h->text); break;
	case ')': copy(h->kind, "ITEM", sizeof h->kind);   copy(h->text, info + 1, sizeof h->text); break;
	case '_': copy(h->kind, "WX", sizeof h->kind);     copy(h->text, info + 1, sizeof h->text); break;
	case 'T': copy(h->kind, "TELEM", sizeof h->kind);  copy(h->text, info + 1, sizeof h->text); break;
	case '}': copy(h->kind, "3RD PT", sizeof h->kind); copy(h->text, info + 1, sizeof h->text); break;
	default:  copy(h->kind, "OTHER", sizeof h->kind);  copy(h->text, info, sizeof h->text); break;
	}
	return true;
}

/* ------------------------------------------------------------------ distance */
/* sine of whole degrees, Q14 (Bhaskara's formula: within 0.2 %) */
static int32_t sin_deg(int32_t d)
{
	int32_t s = 1;
	d %= 360;
	if (d < 0)
		d += 360;
	if (d >= 180) {
		d -= 180;
		s = -1;
	}
	return s * (4 * d * (180 - d) * 16384 / (40500 - d * (180 - d)));
}

static uint32_t isqrt(uint32_t v)
{
	uint32_t r = 0, b = 1u << 30;
	while (b > v)
		b >>= 2;
	while (b) {
		if (v >= r + b) {
			v -= r + b;
			r = (r >> 1) + b;
		} else
			r >>= 1;
		b >>= 2;
	}
	return r;
}

/* degrees of atan(n / d) for 0 <= n <= d, d > 0 */
static int32_t atan_deg(uint32_t n, uint32_t d)
{
	uint32_t z = (uint32_t)((n >> 4) * 1024u / ((d >> 4) ? (d >> 4) : 1u));    /* Q10 */
	if (z > 1024u)
		z = 1024u;
	/* atan z ~ 45 z + 15.64 z (1 - z) degrees */
	return (int32_t)((45u * z + 1564u * z / 100u * (1024u - z) / 1024u + 512u) / 1024u);
}

uint32_t APRS_Distance(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2, bool miles, uint16_t *bearing)
{
	int32_t dy = lat2 - lat1, dl = lon2 - lon1, dx;
	uint32_t ax, ay, hyp;
	unsigned sh = 0;
	int32_t b;

	if (dl > APRS_LON_MAX) dl -= 2 * APRS_LON_MAX;
	if (dl < -APRS_LON_MAX) dl += 2 * APRS_LON_MAX;
	dx = dl * (sin_deg((lat1 + lat2) / 12000 + 90) >> 4) / 1024;   /* times cos(mean latitude); no 64-bit sums */
	ax = (uint32_t)(dx < 0 ? -dx : dx);
	ay = (uint32_t)(dy < 0 ? -dy : dy);
	while ((ax >> sh) > 46000u || (ay >> sh) > 46000u)
		sh++;
	hyp = isqrt((ax >> sh) * (ax >> sh) + (ay >> sh) * (ay >> sh)) << sh;
	if (bearing) {                        /* clockwise from north */
		if (!ax && !ay)
			b = 0;
		else if (ax <= ay)
			b = atan_deg(ax, ay);
		else
			b = 90 - atan_deg(ay, ax);
		if (dy < 0) b = 180 - b;
		if (dx < 0) b = 360 - b;
		*bearing = (uint16_t)(b % 360);
	}
	/* 1/100 minute = 18.52 m = 0.1852 tenths of a km, or 0.11508 tenths of a mile */
	return miles ? hyp / 10u * 11508u / 10000u : hyp / 10u * 1852u / 1000u;
}
