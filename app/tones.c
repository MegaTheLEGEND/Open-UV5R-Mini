/* Open-UV5R-Mini: sub-audio tones. See app/tones.h. */
#include "app/tones.h"

/* The standard DCS codes and 645, in the stock order (C octal literals: the code as a plain number, as stock keeps it). */
const uint16_t TONE_DcsCodes[TONE_DCS_COUNT] = {
	0023, 0025, 0026, 0031, 0032, 0036, 0043, 0047, 0051, 0053, 0054, 0065, 0071, 0072, 0073,
	0074, 0114, 0115, 0116, 0122, 0125, 0131, 0132, 0134, 0143, 0145, 0152, 0155, 0156, 0162,
	0165, 0172, 0174, 0205, 0212, 0223, 0225, 0226, 0243, 0244, 0245, 0246, 0251, 0252, 0255,
	0261, 0263, 0265, 0266, 0271, 0274, 0306, 0311, 0315, 0325, 0331, 0332, 0343, 0346, 0351,
	0356, 0364, 0365, 0371, 0411, 0412, 0413, 0423, 0431, 0432, 0445, 0446, 0452, 0454, 0455,
	0462, 0464, 0465, 0466, 0503, 0506, 0516, 0523, 0526, 0532, 0546, 0565, 0606, 0612, 0624,
	0627, 0631, 0632, 0645, 0654, 0662, 0664, 0703, 0712, 0723, 0731, 0732, 0734, 0743, 0754,
};

const uint16_t TONE_Ctcss[TONE_CTCSS_COUNT] = {
	630, 670, 693, 719, 744, 770, 797, 825, 854, 885, 915, 948, 974, 1000, 1035, 1072, 1109,
	1148, 1188, 1230, 1273, 1318, 1365, 1413, 1462, 1514, 1567, 1598, 1622, 1655, 1679, 1713,
	1738, 1773, 1799, 1835, 1862, 1899, 1928, 1966, 1995, 2035, 2065, 2107, 2181, 2257, 2291,
	2336, 2418, 2503, 2541,
};

ToneKind_t TONE_Kind(uint16_t t)
{
	if (t == 0 || t == 0xFFFFu)
		return TONE_NONE;
	if (t >= 600u)
		return t <= 3000u ? TONE_CTCSS : TONE_NONE;
	if (t <= TONE_DCS_COUNT)
		return TONE_DCS_N;
	if (t <= 2u * TONE_DCS_COUNT)
		return TONE_DCS_I;
	return TONE_NONE;
}

uint16_t TONE_Normalise(uint16_t t)
{
	return TONE_Kind(t) == TONE_NONE ? 0 : t;
}

uint16_t TONE_DcsCode(uint16_t t)
{
	switch (TONE_Kind(t)) {
	case TONE_DCS_N: return TONE_DcsCodes[t - 1u];
	case TONE_DCS_I: return TONE_DcsCodes[t - 1u - TONE_DCS_COUNT];
	default:         return 0;
	}
}

/* Golay (23,12): the 12 data bits are the 9-bit code and 0b100 above it; the 11 check bits are the remainder of
 * data * x^11 divided by the generator x^11 + x^10 + x^6 + x^5 + x^4 + x^2 + 1 (0xC75), as stock sub_01E4D81E. */
uint32_t TONE_Golay(uint16_t code)
{
	uint32_t data = (uint32_t)(code & 0x1FFu) | 0x800u;
	uint32_t r = data << 11;

	for (int bit = 22; bit >= 11; bit--)
		if (r & (1u << bit))
			r ^= 0xC75u << (bit - 11);
	return data | (r & 0x7FFu) << 12;
}

uint16_t TONE_Step(uint16_t t, int dir)
{
	/* positions: 0 off, 1..51 CTCSS, 52..156 DCS N, 157..261 DCS I */
	const int n = 1 + (int)TONE_CTCSS_COUNT + 2 * (int)TONE_DCS_COUNT;
	int pos = 0;

	switch (TONE_Kind(t)) {
	case TONE_CTCSS:
		for (unsigned i = 0; i < TONE_CTCSS_COUNT; i++)
			if (TONE_Ctcss[i] == t)
				pos = 1 + (int)i;
		break;
	case TONE_DCS_N:
	case TONE_DCS_I:
		pos = (int)TONE_CTCSS_COUNT + t;
		break;
	default:
		break;
	}
	pos = ((pos + dir) % n + n) % n;
	if (pos == 0)
		return 0;
	if (pos <= (int)TONE_CTCSS_COUNT)
		return TONE_Ctcss[pos - 1];
	return (uint16_t)(pos - (int)TONE_CTCSS_COUNT);
}

uint16_t TONE_FromCtcss(uint32_t tenths)
{
	for (unsigned i = 0; i < TONE_CTCSS_COUNT; i++) {
		uint32_t t = TONE_Ctcss[i], d = t > tenths ? t - tenths : tenths - t;
		if (d <= 9u)
			return (uint16_t)t;
	}
	return 0;
}

static int rotation_of(uint32_t word, uint32_t want)
{
	for (int r = 0; r < 23; r++) {
		if (word == want)
			return 1;
		word = (word << 1 & 0x7FFFFFu) | (word >> 22 & 1u);
	}
	return 0;
}

uint16_t TONE_FromDcsWord(uint32_t word)
{
	word &= 0x7FFFFFu;
	/* normal codes first, in table order, as stock (some codes alias others under rotation) */
	for (unsigned inv = 0; inv < 2; inv++)
		for (unsigned i = 0; i < TONE_DCS_COUNT; i++)
			if (rotation_of(inv ? word ^ 0x7FFFFFu : word, TONE_Golay(TONE_DcsCodes[i])))
				return (uint16_t)(i + 1u + (inv ? TONE_DCS_COUNT : 0u));
	return 0;
}

char *TONE_Format(char *b, uint16_t t)
{
	char *p = b;
	ToneKind_t k = TONE_Kind(t);

	if (k == TONE_NONE) {
		b[0] = 'O'; b[1] = 'F'; b[2] = 'F'; b[3] = 0;
		return b;
	}
	if (k == TONE_CTCSS) {
		char d[6];
		int n = 0;
		uint16_t w = t / 10u;
		do
			d[n++] = (char)('0' + w % 10u);
		while ((w /= 10u) && n < 5);
		while (n)
			*p++ = d[--n];
		*p++ = '.';
		*p++ = (char)('0' + t % 10u);
		*p = 0;
		return b;
	}
	uint16_t c = TONE_DcsCode(t);
	*p++ = 'D';
	*p++ = (char)('0' + (c >> 6 & 7u));
	*p++ = (char)('0' + (c >> 3 & 7u));
	*p++ = (char)('0' + (c & 7u));
	*p++ = k == TONE_DCS_I ? 'I' : 'N';
	*p = 0;
	return b;
}
