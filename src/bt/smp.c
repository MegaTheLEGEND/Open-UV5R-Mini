/* Open-UV5R-Mini: BLE legacy pairing, peripheral side (bt/smp.h). Our own code, written from the Bluetooth Core
 * Specification (Vol 3 Part H) and FIPS-197 (AES). */
#include "bt/smp.h"

/* ------------------------------------------------------------------ AES-128, encrypt only */
static const uint8_t SBOX[256] = {
	0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
	0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
	0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
	0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
	0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
	0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
	0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
	0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
	0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
	0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
	0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
	0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
	0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
	0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
	0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
	0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16,
};

static uint8_t xt(uint8_t x) { return (uint8_t)((x << 1) ^ ((x & 0x80u) ? 0x1Bu : 0u)); }

void SMP_Aes(const uint8_t key[16], const uint8_t in[16], uint8_t out[16])
{
	uint8_t s[16], k[16], rc = 1;

	for (int i = 0; i < 16; i++) {
		k[i] = key[i];
		s[i] = (uint8_t)(in[i] ^ key[i]);
	}
	for (int round = 1; round <= 10; round++) {
		uint8_t t[16];
		/* SubBytes and ShiftRows (column-major state) */
		for (int c = 0; c < 4; c++)
			for (int r = 0; r < 4; r++)
				t[4 * c + r] = SBOX[s[4 * ((c + r) & 3) + r]];
		/* MixColumns, except in the last round */
		if (round < 10)
			for (int c = 0; c < 4; c++) {
				uint8_t *p = t + 4 * c, a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3], all = a0 ^ a1 ^ a2 ^ a3;
				p[0] ^= (uint8_t)(all ^ xt((uint8_t)(a0 ^ a1)));
				p[1] ^= (uint8_t)(all ^ xt((uint8_t)(a1 ^ a2)));
				p[2] ^= (uint8_t)(all ^ xt((uint8_t)(a2 ^ a3)));
				p[3] ^= (uint8_t)(all ^ xt((uint8_t)(a3 ^ a0)));
			}
		/* the next round key */
		k[0] ^= (uint8_t)(SBOX[k[13]] ^ rc);
		k[1] ^= SBOX[k[14]];
		k[2] ^= SBOX[k[15]];
		k[3] ^= SBOX[k[12]];
		for (int i = 4; i < 16; i++)
			k[i] ^= k[i - 4];
		rc = xt(rc);
		for (int i = 0; i < 16; i++)
			s[i] = (uint8_t)(t[i] ^ k[i]);
	}
	for (int i = 0; i < 16; i++)
		out[i] = s[i];
}

/* e(k, p) of the Bluetooth spec on little-endian values: AES works most significant byte first */
static void e_le(const uint8_t k[16], const uint8_t p[16], uint8_t out[16])
{
	uint8_t kb[16], pb[16], ob[16];
	for (int i = 0; i < 16; i++) {
		kb[i] = k[15 - i];
		pb[i] = p[15 - i];
	}
	SMP_Aes(kb, pb, ob);
	for (int i = 0; i < 16; i++)
		out[i] = ob[15 - i];
}

void SMP_C1(const uint8_t k[16], const uint8_t r[16], const uint8_t preq[7], const uint8_t pres[7], uint8_t iat,
            const uint8_t ia[6], uint8_t rat, const uint8_t ra[6], uint8_t out[16])
{
	uint8_t p[16], t[16];

	/* p1 = pres || preq || rat' || iat' (iat' least significant) */
	p[0] = iat;
	p[1] = rat;
	for (int i = 0; i < 7; i++) {
		p[2 + i] = preq[i];
		p[9 + i] = pres[i];
	}
	for (int i = 0; i < 16; i++)
		t[i] = (uint8_t)(r[i] ^ p[i]);
	e_le(k, t, t);
	/* p2 = padding || ia || ra (ra least significant) */
	for (int i = 0; i < 6; i++) {
		p[i] = ra[i];
		p[6 + i] = ia[i];
	}
	p[12] = p[13] = p[14] = p[15] = 0;
	for (int i = 0; i < 16; i++)
		t[i] ^= p[i];
	e_le(k, t, out);
}

void SMP_S1(const uint8_t k[16], const uint8_t r1[16], const uint8_t r2[16], uint8_t out[16])
{
	uint8_t r[16];
	for (int i = 0; i < 8; i++) {
		r[i] = r2[i];                    /* r' = r1' || r2', r2' the least significant half */
		r[8 + i] = r1[i];
	}
	e_le(k, r, out);
}

/* ------------------------------------------------------------------ the pairing */
#define OP_PAIR_REQ   0x01
#define OP_PAIR_RSP   0x02
#define OP_CONFIRM    0x03
#define OP_RANDOM     0x04
#define OP_FAILED     0x05
#define OP_ENC_INFO   0x06
#define OP_MASTER_ID  0x07
#define OP_SEC_REQ    0x0B

#define R_PASSKEY_FAILED  0x01
#define R_AUTH_REQ        0x03
#define R_CONFIRM_FAILED  0x04
#define R_NOT_SUPPORTED   0x05
#define R_ENC_KEY_SIZE    0x06
#define R_UNSPECIFIED     0x08
#define R_INVALID         0x0A

#define IO_DISPLAY_ONLY   0x00
#define AUTH_BOND         0x01
#define AUTH_MITM         0x04

static struct {
	uint8_t      ia[6], ra[6], iat, rat;
	uint8_t      er[16];
	SMP_Random_t rnd;
	bool         pairable;
	uint8_t      state;                  /* SmpState_t */
	uint8_t      step;                   /* 0 nothing, 1 response sent, 2 confirm received, 3 random checked */
	uint8_t      preq[7], pres[7];
	uint8_t      tk[16], mconfirm[16], srand[16], stk[16];
	int32_t      passkey;
	bool         bond, encrypted, keys_due;
	uint8_t      reason;
} S;

static void zero(uint8_t *p, int n) { while (n--) *p++ = 0; }
static bool same(const uint8_t *a, const uint8_t *b, int n)
{
	uint8_t d = 0;
	while (n--)
		d |= (uint8_t)(*a++ ^ *b++);
	return d == 0;
}

/* the long-term key for (ediv, rand): AES_ER of them, so it can be made again later */
static void ltk_for(uint16_t ediv, const uint8_t rand[8], uint8_t ltk[16])
{
	uint8_t d[16];
	zero(d, 16);
	d[0] = (uint8_t)ediv;
	d[1] = (uint8_t)(ediv >> 8);
	for (int i = 0; i < 8; i++)
		d[2 + i] = rand[i];
	d[15] = 0x4C;                        /* 'L': keeps these apart from anything else made from ER */
	e_le(S.er, d, ltk);
}

void SMP_Reset(const uint8_t ia[6], uint8_t iat, const uint8_t ra[6], uint8_t rat, const uint8_t er[16],
               SMP_Random_t rnd)
{
	bool pairable = S.pairable;
	zero((uint8_t *)&S, (int)sizeof S);
	S.pairable = pairable;
	for (int i = 0; i < 6; i++) {
		S.ia[i] = ia[i];
		S.ra[i] = ra[i];
	}
	S.iat = iat;
	S.rat = rat;
	for (int i = 0; i < 16; i++)
		S.er[i] = er[i];
	S.rnd = rnd;
	S.passkey = -1;
}

void SMP_SetPairable(bool on) { S.pairable = on; }
bool SMP_Pairable(void) { return S.pairable; }
SmpState_t SMP_State(void) { return (SmpState_t)S.state; }
int32_t SMP_Passkey(void) { return S.state == SMP_PAIRING ? S.passkey : -1; }
uint8_t SMP_FailReason(void) { return S.reason; }
bool SMP_IsEncrypted(void) { return S.encrypted; }

static int fail(uint8_t *out, uint8_t reason)
{
	S.state = SMP_FAILED;
	S.step = 0;
	S.passkey = -1;
	S.reason = reason;
	out[0] = OP_FAILED;
	out[1] = reason;
	return 2;
}

int SMP_Input(const uint8_t *in, int len, uint8_t *out)
{
	if (len < 1)
		return 0;
	switch (in[0]) {
	case OP_PAIR_REQ: {
		uint8_t io, auth, maxkey, rdist;
		if (len != 7)
			return fail(out, R_INVALID);
		if (!S.pairable)
			return fail(out, R_NOT_SUPPORTED);
		io = in[1];
		auth = in[3];
		maxkey = in[4];
		rdist = in[6];
		if (maxkey < 7 || maxkey > 16)
			return fail(out, R_ENC_KEY_SIZE);
		for (int i = 0; i < 7; i++)
			S.preq[i] = in[i];
		S.bond = (auth & 3u) == AUTH_BOND && (rdist & 1u);
		/* the method: passkey entry when the phone asks for MITM and can type (KeyboardOnly 2, KeyboardDisplay 4) */
		zero(S.tk, 16);
		S.passkey = -1;
		if ((auth & AUTH_MITM) && (io == 0x02 || io == 0x04)) {
			uint8_t r[4];
			S.rnd(r, 4);
			S.passkey = (int32_t)(((uint32_t)r[0] | (uint32_t)r[1] << 8 | (uint32_t)r[2] << 16 |
			                       (uint32_t)r[3] << 24) % 1000000u);
			S.tk[0] = (uint8_t)S.passkey;
			S.tk[1] = (uint8_t)(S.passkey >> 8);
			S.tk[2] = (uint8_t)(S.passkey >> 16);
		}
		out[0] = OP_PAIR_RSP;
		out[1] = IO_DISPLAY_ONLY;
		out[2] = 0;                      /* no out-of-band data */
		out[3] = (uint8_t)((S.bond ? AUTH_BOND : 0) | (S.passkey >= 0 ? AUTH_MITM : 0));   /* no SC, no keypress */
		out[4] = 16;
		out[5] = 0;                      /* the phone hands us no keys */
		out[6] = S.bond ? 0x01 : 0x00;   /* we hand out the encryption key (LTK, EDIV, Rand) */
		for (int i = 0; i < 7; i++)
			S.pres[i] = out[i];
		S.state = SMP_PAIRING;
		S.step = 1;
		S.reason = 0;
		return 7;
	}
	case OP_CONFIRM:
		if (S.step != 1 || len != 17)
			return fail(out, R_UNSPECIFIED);
		for (int i = 0; i < 16; i++)
			S.mconfirm[i] = in[1 + i];
		S.rnd(S.srand, 16);
		out[0] = OP_CONFIRM;
		SMP_C1(S.tk, S.srand, S.preq, S.pres, S.iat, S.ia, S.rat, S.ra, out + 1);
		S.step = 2;
		return 17;
	case OP_RANDOM: {
		uint8_t c[16];
		if (S.step != 2 || len != 17)
			return fail(out, R_UNSPECIFIED);
		SMP_C1(S.tk, in + 1, S.preq, S.pres, S.iat, S.ia, S.rat, S.ra, c);
		if (!same(c, S.mconfirm, 16))
			return fail(out, S.passkey >= 0 ? R_PASSKEY_FAILED : R_CONFIRM_FAILED);
		SMP_S1(S.tk, S.srand, in + 1, S.stk);
		out[0] = OP_RANDOM;
		for (int i = 0; i < 16; i++)
			out[1 + i] = S.srand[i];
		S.step = 3;
		return 17;
	}
	case OP_FAILED:
		S.state = SMP_FAILED;
		S.step = 0;
		S.passkey = -1;
		S.reason = len > 1 ? in[1] : R_UNSPECIFIED;
		return 0;
	case OP_PAIR_RSP:
	case OP_SEC_REQ:
		return 0;                        /* the central's packets only */
	default:
		if (S.step)                      /* keys from the phone: we asked for none */
			return 0;
		return 0;
	}
}

bool SMP_LongTermKey(const uint8_t rand[8], uint16_t ediv, uint8_t ltk[16])
{
	bool zero_id = ediv == 0;
	for (int i = 0; i < 8; i++)
		zero_id = zero_id && rand[i] == 0;
	if (zero_id) {                       /* the short-term key, during pairing */
		if (S.step != 3)
			return false;
		for (int i = 0; i < 16; i++)
			ltk[i] = S.stk[i];
		S.keys_due = S.bond;
		return true;
	}
	ltk_for(ediv, rand, ltk);            /* a phone paired before */
	return true;
}

int SMP_Encrypted(uint8_t *out1, int *len1, uint8_t *out2, int *len2)
{
	uint8_t rand[8], ediv[2];

	S.encrypted = true;
	if (S.step == 3) {
		S.state = SMP_PAIRED;
		S.step = 0;
		S.passkey = -1;
	} else if (S.state != SMP_PAIRING)
		S.state = SMP_PAIRED;            /* a phone paired before, back with its key */
	if (!S.keys_due)
		return 0;
	S.keys_due = false;
	S.rnd(rand, 8);
	S.rnd(ediv, 2);
	out1[0] = OP_ENC_INFO;
	ltk_for((uint16_t)(ediv[0] | ediv[1] << 8), rand, out1 + 1);
	*len1 = 17;
	out2[0] = OP_MASTER_ID;
	out2[1] = ediv[0];
	out2[2] = ediv[1];
	for (int i = 0; i < 8; i++)
		out2[3 + i] = rand[i];
	*len2 = 11;
	return 2;
}
