/* Open-UV5R-Mini: RDS receive. See radio/rds.h. Our own code, from the public RDS standard (IEC 62106). */
#include "radio/rds.h"

#if defined(__PI32V2__)
#define RDS_RAMFUNC __attribute__((section(".ramfunc.rds")))
#else
#define RDS_RAMFUNC
#endif

/* ------------------------------------------------------------------------------- the interrupt half */

/* sin(2 pi i / 256) * 16384, the source of the RAM tables below */
static const int16_t SIN14[256] = {
	0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897,
	6270, 6639, 7005, 7366, 7723, 8076, 8423, 8765, 9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297,
	11585, 11866, 12140, 12406, 12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978,
	15137, 15286, 15426, 15557, 15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379,
	16384, 16379, 16364, 16340, 16305, 16261, 16207, 16143, 16069, 15986, 15893, 15791, 15679, 15557, 15426, 15286,
	15137, 14978, 14811, 14635, 14449, 14256, 14053, 13842, 13623, 13395, 13160, 12916, 12665, 12406, 12140, 11866,
	11585, 11297, 11003, 10702, 10394, 10080, 9760, 9434, 9102, 8765, 8423, 8076, 7723, 7366, 7005, 6639,
	6270, 5897, 5520, 5139, 4756, 4370, 3981, 3590, 3196, 2801, 2404, 2006, 1606, 1205, 804, 402,
	0, -402, -804, -1205, -1606, -2006, -2404, -2801, -3196, -3590, -3981, -4370, -4756, -5139, -5520, -5897,
	-6270, -6639, -7005, -7366, -7723, -8076, -8423, -8765, -9102, -9434, -9760, -10080, -10394, -10702, -11003, -11297,
	-11585, -11866, -12140, -12406, -12665, -12916, -13160, -13395, -13623, -13842, -14053, -14256, -14449, -14635, -14811, -14978,
	-15137, -15286, -15426, -15557, -15679, -15791, -15893, -15986, -16069, -16143, -16207, -16261, -16305, -16340, -16364, -16379,
	-16384, -16379, -16364, -16340, -16305, -16261, -16207, -16143, -16069, -15986, -15893, -15791, -15679, -15557, -15426, -15286,
	-15137, -14978, -14811, -14635, -14449, -14256, -14053, -13842, -13623, -13395, -13160, -12916, -12665, -12406, -12140, -11866,
	-11585, -11297, -11003, -10702, -10394, -10080, -9760, -9434, -9102, -8765, -8423, -8076, -7723, -7366, -7005, -6639,
	-6270, -5897, -5520, -5139, -4756, -4370, -3981, -3590, -3196, -2801, -2404, -2006, -1606, -1205, -804, -402,
};
/* cos and sin * 256, made at reset: in RAM beside the RAM code that reads them, and small enough that the products
 * need no shift in the loop */
__attribute__((section(".data.rds_tab"))) static int16_t s_cos8[256] = { 1 };
__attribute__((section(".data.rds_tab"))) static int16_t s_sin8[256] = { 1 };

#define RING       512u                  /* 8 values a bit: 54 ms; the main loop reads every 10 ms */

static volatile uint32_t s_wr, s_rd;
static int32_t s_ri[RING], s_rq[RING];

static uint32_t s_ph, s_inc, s_inc0;     /* pilot phase and step, 2^32 a cycle */
static uint32_t s_cyc, s_dumped;         /* pilot cycles, and the count at the last value handed on */
static int32_t  s_aI, s_aQ;              /* 57 kHz sums */
static int32_t  s_lockq;                 /* the pilot loop's phase error, smoothed (4096 = 1 radian) */
static volatile bool s_lock, s_fe_reset;
static uint32_t s_fs = 599552u;          /* 1171 FM interrupts a second x 512 (Sean's radio, 2026-11-26) */

/* The cost matters: this runs on 600 000 samples a second inside the FM interrupt. The first version (a sample at a
 * time, the pilot on every sample, a click filter) took about 0.65 ms of every 0.85 ms block on Sean's radio
 * (2026-11-26) and starved the main loop until the watchdog restarted the radio. Now: samples in pairs (their sum,
 * at the pair's middle; it loses about 0.4 dB at 57 kHz and lets in a little noise from near 243 kHz), the 57 kHz
 * carrier on every pair, the pilot on every 3rd pair (it is strong), no click filter (JieLi's demodulator, which
 * now runs, cut the clicks about 8 times). */
RDS_RAMFUNC void RDS_FeBlock(const volatile int32_t *x, uint32_t n)
{
	uint32_t ph, inc2, ph3, inc6, cyc, dumped, nph, half, k = 0;
	int32_t corr, aI, aQ, pI = 0, pQ = 0, e, ai;

	if (s_fe_reset) {
		s_fe_reset = false;
		s_ph = 0;
		s_inc = s_inc0;
		s_cyc = s_dumped = 0;
		s_aI = s_aQ = 0;
		s_lockq = 4096;
		s_lock = false;
	}
	ph = s_ph;
	half = s_inc >> 1;                   /* a pair's middle is half a sample on */
	inc2 = s_inc * 2u;
	ph3 = (ph + half) * 3u;              /* the 57 kHz carrier: 3 times the pilot's phase */
	inc6 = inc2 * 3u;
	cyc = s_cyc;
	dumped = s_dumped;
	aI = s_aI;
	aQ = s_aQ;
	for (uint32_t i = 0; i + 1u < n; i += 2u) {
		int32_t v = x[i] + x[i + 1u];
		uint32_t p3 = ph3 >> 24;

		aI += v * s_cos8[p3];
		aQ += v * s_sin8[p3];
		if (k-- == 0u) {                 /* the pilot, every 3rd pair: 100 000 a second, where nothing folds onto
		                                  * 19 kHz (every 4th, 75 000, folded the RDS at 57 kHz onto it) */
			k = 2u;
			uint32_t p = (ph + half) >> 24;
			pI += (v * s_cos8[p]) >> 4;
			pQ += (v * s_sin8[p]) >> 4;
		}
		ph3 += inc6;
		ph += inc2;
		/* every 2 pilot cycles: 8 values a bit (the bit is 16 cycles). A count already handed on is not handed on
		 * again: the loop's phase step can carry the phase back over a cycle boundary (below), and the same
		 * boundary is then crossed twice. */
		if (ph < inc2 && (++cyc & 1u) == 0u && cyc != dumped) {
			uint32_t w = s_wr;
			dumped = cyc;
			if (w - s_rd < RING) {
				s_ri[w % RING] = aI >> 12;
				s_rq[w % RING] = aQ >> 12;
				s_wr = w + 1u;
			}
			aI = aQ = 0;
		}
	}
	/* the pilot loop, once a block (about 1170 a second): locked when pI > 0 and pQ = 0. The error is pQ / |pI| in
	 * 1/4096 radian (small angles); wide while it pulls in, narrow once locked. */
	ai = pI < 0 ? -pI : pI;
	if (ai < 4096)
		e = 0;                           /* no pilot to speak of */
	else if (pI < 0)
		e = pQ > 0 ? 4096 : -4096;       /* half a turn off: push round */
	else {
		e = pQ / (ai >> 12);
		if (e > 4096)
			e = 4096;
		else if (e < -4096)
			e = -4096;
	}
	if (s_lock) {
		corr = e * 6342;                 /* natural frequency about 5 Hz */
		s_inc -= (uint32_t)(e / 4);
	} else {
		corr = e * 37500;                /* about 30 Hz */
		s_inc -= (uint32_t)(e * 8);
	}
	nph = ph - (uint32_t)corr;           /* keep the cycle count right when the step crosses a cycle boundary */
	if (corr > 0 && nph > ph)
		cyc--;
	else if (corr < 0 && nph < ph)
		cyc++;
	ph = nph;
	if (s_inc - s_inc0 + (s_inc0 >> 7) > (s_inc0 >> 6))   /* more than 0.8% off: start again */
		s_inc = s_inc0;
	s_lockq += ((ai < 4096 ? 4096 : (e < 0 ? -e : e)) - s_lockq) >> 5;
	if (!s_lock && pI > 0 && s_lockq < 800)
		s_lock = true;
	else if (s_lock && s_lockq > 2000)   /* wide again only when really lost: a noisy pilot (it is measured on a
	                                      * third of the pairs) would flip it back and forth */
		s_lock = false;
	s_ph = ph;
	s_cyc = cyc;
	s_dumped = dumped;
	s_aI = aI;
	s_aQ = aQ;
}

/* ------------------------------------------------------------------------------- the main-loop half */

#define POLY 0x5B9u                      /* x^10 + x^8 + x^7 + x^5 + x^4 + x^3 + 1 */
static const uint16_t OFFSET[5] = { 0x0FC, 0x198, 0x168, 0x350, 0x1B4 };   /* A B C C' D */
static const uint8_t  SLOT[5] = { 0, 1, 2, 2, 3 };                         /* where each sits in a group */

/* the carrier phase is not known (it depends on the filters in the FM block): 8 angles over half a turn, and
 * 8 bit timings; the pair with the most energy wins (worst loss at 11.25 degrees: 0.2 dB) */
static const int16_t COS8[8] = { 256, 237, 181, 98, 0, -98, -181, -237 };
static const int16_t SIN8[8] = { 0, 98, 181, 237, 256, 237, 181, 98 };

static RdsInfo_t s_info;
static uint16_t s_syn1[26];              /* the syndrome of each single-bit error */
static int32_t  s_hist[8][8];            /* [angle][sample % 8] */
static int32_t  s_energy[8][8];          /* [angle][timing] */
static uint32_t s_n;
static uint8_t  s_ax, s_tk;
static uint8_t  s_prevb;
static uint32_t s_w, s_bits, s_last_pos;
static int      s_last_t;
static uint8_t  s_exp, s_nb;
static int      s_bad;
static uint16_t s_g[4];
static uint8_t  s_mask;
static uint16_t s_pi_cand, s_ps_cand[4];
static uint8_t  s_rt_ab;
static bool     s_changed;

static uint16_t syndrome(uint32_t w)
{
	for (int i = 25; i >= 10; i--)
		if (w & (1u << i))
			w ^= POLY << (i - 10);
	return (uint16_t)(w & 0x3FFu);
}

/* the offset that block w carries (0..4), or -1 */
static int block_type(uint32_t w)
{
	uint16_t s = syndrome(w);

	for (int t = 0; t < 5; t++)
		if (s == OFFSET[t])
			return t;
	return -1;
}

/* correct w for offset t: one wrong bit, or two within 5 bits of each other; false if not */
static bool correct(uint32_t *w, int t)
{
	uint16_t want = (uint16_t)(syndrome(*w) ^ OFFSET[t]);

	if (!want)
		return true;
	for (int i = 0; i < 26; i++) {
		if (s_syn1[i] == want) {
			*w ^= 1u << i;
			return true;
		}
		for (int j = i + 1; j < 26 && j <= i + 4; j++)
			if ((uint16_t)(s_syn1[i] ^ s_syn1[j]) == want) {
				*w ^= 1u << i | 1u << j;
				return true;
			}
	}
	return false;
}

static char show(uint8_t c)
{
	return c >= 32u && c < 127u ? (char)c : ' ';
}

static void set_char(char *s, unsigned i, uint8_t c)
{
	char ch = show(c);

	if (s[i] != ch) {
		s[i] = ch;
		s_changed = true;
	}
}

static void group_done(void)
{
	uint16_t b = s_g[1];
	unsigned type = b >> 12, ver = (b >> 11) & 1u;

	if (!(s_mask & 2u))
		return;
	s_info.groups++;
	if (s_mask & 1u) {                   /* PI: the same twice before it is shown */
		if (s_g[0] == s_pi_cand && s_info.pi != s_g[0]) {
			s_info.pi = s_g[0];
			s_changed = true;
		}
		s_pi_cand = s_g[0];
	}
	if (type == 0u && (s_mask & 8u)) {   /* 0A/0B: two characters of the station name */
		unsigned seg = b & 3u;
		if (s_ps_cand[seg] == s_g[3]) {
			set_char(s_info.ps, 2u * seg, (uint8_t)(s_g[3] >> 8));
			set_char(s_info.ps, 2u * seg + 1u, (uint8_t)s_g[3]);
		}
		s_ps_cand[seg] = s_g[3];
	} else if (type == 2u) {             /* 2A: four characters of radio text, 2B: two */
		unsigned seg = b & 15u, ab = (b >> 4) & 1u;
		if (ab != s_rt_ab) {             /* the text A/B flag flipped: new text */
			s_rt_ab = (uint8_t)ab;
			for (unsigned i = 0; i < 64u; i++)
				s_info.rt[i] = ' ';
			s_changed = true;
		}
		if (ver == 0u && (s_mask & 12u) == 12u) {
			set_char(s_info.rt, 4u * seg, (uint8_t)(s_g[2] >> 8));
			set_char(s_info.rt, 4u * seg + 1u, (uint8_t)s_g[2]);
			set_char(s_info.rt, 4u * seg + 2u, (uint8_t)(s_g[3] >> 8));
			set_char(s_info.rt, 4u * seg + 3u, (uint8_t)s_g[3]);
		} else if (ver == 1u && (s_mask & 8u)) {
			set_char(s_info.rt, 2u * seg, (uint8_t)(s_g[3] >> 8));
			set_char(s_info.rt, 2u * seg + 1u, (uint8_t)s_g[3]);
		}
	}
}

/* a block for slot s (0 A .. 3 D) */
static void block(uint32_t w, unsigned slot)
{
	bool ok;

	if (slot == 0u)
		s_mask = 0;
	if (slot == 2u)
		ok = correct(&w, 2) || correct(&w, 3);
	else
		ok = correct(&w, slot == 3u ? 4 : (int)slot);
	if (ok) {
		s_info.blocks_ok++;
		s_g[slot] = (uint16_t)(w >> 10);
		s_mask |= (uint8_t)(1u << slot);
		if (s_bad > 0)
			s_bad--;
	} else {
		s_info.blocks_bad++;
		s_bad += 2;
		if (s_bad > 40) {                /* lost: look for sync again */
			s_info.sync = false;
			s_changed = true;
			s_last_t = -1;
		}
	}
	if (slot == 3u)
		group_done();
}

static void bit_in(uint8_t b)
{
	uint8_t d = b ^ s_prevb;             /* differential decoding */

	s_prevb = b;
	s_w = ((s_w << 1) | d) & 0x3FFFFFFu;
	s_bits++;
	if (s_info.sync) {
		if (++s_nb == 26u) {
			s_nb = 0;
			block(s_w, s_exp);
			s_exp = (uint8_t)((s_exp + 1u) & 3u);
		}
		return;
	}
	int t = block_type(s_w);             /* no sync: two right blocks 26 bits apart, in order */
	if (t < 0)
		return;
	if (s_last_t >= 0 && s_bits - s_last_pos == 26u && SLOT[t] == ((SLOT[s_last_t] + 1u) & 3u)) {
		s_info.sync = true;
		s_changed = true;
		s_bad = 0;
		s_nb = 0;
		s_mask = 0;                      /* the block before is not kept: the first whole group starts at the next A */
		block(s_w, SLOT[t]);
		s_exp = (uint8_t)((SLOT[t] + 1u) & 3u);
		return;
	}
	s_last_t = t;
	s_last_pos = s_bits;
}

static void sample(int32_t i, int32_t q)
{
	unsigned k = s_n & 7u;
	int32_t bitv = 0;

	for (unsigned a = 0; a < 8u; a++) {
		int32_t *h = s_hist[a], v, av;
		h[k] = (i * COS8[a] + q * SIN8[a]) >> 8;
		/* biphase: the first half of the bit minus the second (the last 8 values end at k) */
		v = h[(k + 1u) & 7u] + h[(k + 2u) & 7u] + h[(k + 3u) & 7u] + h[(k + 4u) & 7u]
		  - h[(k + 5u) & 7u] - h[(k + 6u) & 7u] - h[(k + 7u) & 7u] - h[k];
		av = v < 0 ? -v : v;
		s_energy[a][k] += (av - s_energy[a][k]) >> 5;
		if (a == s_ax && k == s_tk)
			bitv = v;
	}
	if (k == s_tk)
		bit_in(bitv > 0);
	if ((++s_n & 255u) == 0u) {          /* every 32 bits: the best angle and timing, with some hysteresis */
		int32_t best = s_energy[s_ax][s_tk];
		uint8_t ba = s_ax, bt = s_tk;
		for (unsigned a = 0; a < 8u; a++)
			for (unsigned t = 0; t < 8u; t++)
				if (s_energy[a][t] > best + (best >> 2)) {
					best = s_energy[a][t];
					ba = (uint8_t)a;
					bt = (uint8_t)t;
				}
		s_ax = ba;
		s_tk = bt;
	}
}

void RDS_Reset(uint32_t fs_mpx)
{
	uint32_t num = 19000u, inc = 0;

	if (fs_mpx)
		s_fs = fs_mpx;
	for (int i = 0; i < 32; i++) {       /* 19000 / fs * 2^32, without 64-bit division */
		num <<= 1;
		inc <<= 1;
		if (num >= s_fs) {
			num -= s_fs;
			inc |= 1u;
		}
	}
	s_inc0 = inc;
	for (int i = 0; i < 256; i++) {
		s_cos8[i] = (int16_t)(SIN14[(i + 64) & 255] / 64);
		s_sin8[i] = (int16_t)(SIN14[i] / 64);
	}
	s_fe_reset = true;
	s_rd = s_wr;
	for (int i = 0; i < 26; i++)
		s_syn1[i] = syndrome(1u << i);
	for (unsigned a = 0; a < 8u; a++)
		for (unsigned t = 0; t < 8u; t++)
			s_hist[a][t] = s_energy[a][t] = 0;
	s_n = 0;
	s_ax = s_tk = 0;
	s_prevb = 0;
	s_w = s_bits = s_last_pos = 0;
	s_last_t = -1;
	s_exp = s_nb = 0;
	s_bad = 0;
	s_mask = 0;
	s_pi_cand = 0;
	for (int i = 0; i < 4; i++)
		s_ps_cand[i] = 0;
	s_rt_ab = 2;
	s_info = (RdsInfo_t){ 0 };
	for (int i = 0; i < 8; i++)
		s_info.ps[i] = ' ';
	for (int i = 0; i < 64; i++)
		s_info.rt[i] = ' ';
	s_changed = true;
}

bool RDS_Poll(void)
{
	uint32_t w = s_wr;

	if (w - s_rd > RING)
		s_rd = w - RING;
	while (s_rd != w) {
		sample(s_ri[s_rd % RING], s_rq[s_rd % RING]);
		s_rd++;
	}
	if (s_info.pilot != s_lock) {
		s_info.pilot = s_lock;
		s_changed = true;
	}
	bool c = s_changed;
	s_changed = false;
	return c;
}

const RdsInfo_t *RDS_Info(void)
{
	return &s_info;
}
