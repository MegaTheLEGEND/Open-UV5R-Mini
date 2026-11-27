/* Open-UV5R-Mini: OOK remote codes from a capture. See radio/ook.h. */
#include "radio/ook.h"

#define MAX_KEYS 4u

/* one frame of 24 bits starting at d[i] (a pulse); te from the gap before it; returns true and the key */
static bool frame(const int32_t *d, unsigned n, unsigned i, uint32_t te, uint32_t *key)
{
	uint32_t k = 0;

	if (i + 47u >= n + 1u)               /* 24 pulses and 23 gaps at least (the last gap is the next sync) */
		return false;
	for (unsigned b = 0; b < 24u; b++) {
		int32_t hi = d[i + 2u * b], lo = (i + 2u * b + 1u < n) ? d[i + 2u * b + 1u] : -(int32_t)(31u * te);
		uint32_t h, l;
		if (hi <= 0 || lo >= 0)
			return false;
		h = (uint32_t)hi;
		l = (uint32_t)-lo;
		k <<= 1;
		if (h * 2u < te * 3u) {          /* a short pulse: 0, its gap about 3 te (or the sync after bit 23) */
			if (b < 23u && (l * 2u < te * 4u || l > te * 5u))
				return false;
		} else if (h * 2u >= te * 4u && h <= te * 5u) {   /* a long pulse: 1, a short gap */
			k |= 1u;
			if (b < 23u && (l * 2u >= te * 3u))
				return false;
		} else
			return false;
	}
	*key = k;
	return true;
}

void OOK_Princeton(const int32_t *d, unsigned n, OokCode_t *out)
{
	uint32_t keys[MAX_KEYS], tes[MAX_KEYS];
	uint8_t cnt[MAX_KEYS];
	unsigned nk = 0, best = 0, total = 0;

	out->ok = false;
	out->bits = 24;
	out->key = 0;
	out->te = 0;
	out->frames = out->other = 0;
	for (unsigned i = 1; i < n; i++) {
		uint32_t gap, te, key;
		if (d[i - 1] >= 0)
			continue;
		gap = (uint32_t)-d[i - 1];
		te = (gap + 15u) / 31u;          /* the sync gap is 31 short units */
		if (te < 80u || te > 1500u)
			continue;
		if (i >= 2 && d[i - 2] > 0 && ((uint32_t)d[i - 2] * 2u > te * 3u))
			continue;                    /* the pulse before the sync is one short unit */
		if (!frame(d, n, i, te, &key))
			continue;
		total++;
		{
			unsigned j;
			for (j = 0; j < nk && keys[j] != key; j++)
				;
			if (j == nk && nk < MAX_KEYS) {
				keys[nk] = key;
				tes[nk] = 0;
				cnt[nk++] = 0;
			}
			if (j < nk) {
				if (cnt[j] < 255u)
					cnt[j]++;
				tes[j] += te;
			}
		}
		i += 46u;                        /* past this frame */
	}
	for (unsigned j = 1; j < nk; j++)
		if (cnt[j] > cnt[best])
			best = j;
	if (!nk)
		return;
	out->ok = true;
	out->key = keys[best];
	out->te = (uint16_t)(tes[best] / cnt[best]);
	out->frames = cnt[best];
	out->other = (uint8_t)(total - cnt[best] > 255u ? 255u : total - cnt[best]);
}
