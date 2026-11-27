/* Open-UV5R-Mini: software AFSK demodulator. See radio/afsk.h. */
#include "radio/afsk.h"

static int16_t s_sin[256];               /* one turn, Q15 */
static bool    s_table;

/* Bhaskara's sine on 0..pi (error under 0.2 %), mirrored for the second half */
static void make_table(void)
{
	for (int i = 0; i < 128; i++) {
		int64_t x = i, p = 128 - i;      /* x in 1/128ths of pi */
		int64_t num = 16 * x * p, den = 5 * 128 * 128 - 4 * x * p;
		int32_t v = (int32_t)(num * 32767 / den);
		s_sin[i] = (int16_t)v;
		s_sin[i + 128] = (int16_t)-v;
	}
	s_table = true;
}

void AFSK_Init(Afsk_t *d, uint32_t fs)
{
	uint32_t n = (fs + 600u) / 1200u, big;

	if (!s_table)
		make_table();
	if (n < 4u)
		n = 4u;
	if (n > AFSK_MAX_N)
		n = AFSK_MAX_N;
	d->n = (uint8_t)n;
	d->pos = 0;
	/* a full-scale sum over n samples is n * 32768; shifted so its square, twice, stays in an int32 */
	for (d->shift = 0, big = n * 32768u; (big >> d->shift) > 23000u; d->shift++)
		;
	d->inc[0] = (uint32_t)(((uint64_t)1200u << 32) / fs);
	d->inc[1] = (uint32_t)(((uint64_t)2200u << 32) / fs);
	d->ph[0] = d->ph[1] = 0;
	for (int k = 0; k < 4; k++) {
		d->sum[k] = 0;
		for (unsigned i = 0; i < AFSK_MAX_N; i++)
			d->ring[k][i] = 0;
	}
	d->dc = 0;
	d->pll = 0;
	d->pll_inc = d->inc[0];              /* one turn a bit */
	d->level = false;
	AX25_RxInit(&d->ax);
}

static int32_t energy(const Afsk_t *d, int k)
{
	int32_t i = d->sum[k] >> d->shift, q = d->sum[k + 1] >> d->shift;
	return i * i + q * q;
}

unsigned AFSK_Sample(Afsk_t *d, int16_t x)
{
	int32_t v;
	uint32_t was;
	bool level;
	unsigned got = 0;

	d->dc += ((int32_t)x * 256 - d->dc) >> 7;
	v = (int32_t)x - (d->dc >> 8);
	if (v > 32767) v = 32767;
	if (v < -32768) v = -32768;
	for (int t = 0; t < 2; t++) {
		uint8_t a = (uint8_t)(d->ph[t] >> 24);
		int32_t pi = v * s_sin[(uint8_t)(a + 64u)] >> 15, pq = v * s_sin[a] >> 15;
		int k = t * 2;
		d->sum[k] += pi - d->ring[k][d->pos];
		d->ring[k][d->pos] = pi;
		d->sum[k + 1] += pq - d->ring[k + 1][d->pos];
		d->ring[k + 1][d->pos] = pq;
		d->ph[t] += d->inc[t];
	}
	if (++d->pos >= d->n)
		d->pos = 0;

	level = energy(d, 0) > energy(d, 2);
	if (level != d->level) {             /* a change belongs half way between two samples of the bit clock */
		int32_t p = (int32_t)d->pll;
		d->pll = (uint32_t)(p - p / 4);
		d->level = level;
	}
	was = d->pll;
	d->pll += d->pll_inc;
	if (was < 0x80000000u && d->pll >= 0x80000000u)   /* the middle of a bit */
		got = AX25_RxBit(&d->ax, d->level);
	return got;
}
