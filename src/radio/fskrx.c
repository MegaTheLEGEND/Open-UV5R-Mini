/* Open-UV5R-Mini: APRS receive through the BK4819's FSK modem (experimental). See radio/fskrx.h. */
#include "radio/fskrx.h"
#include "driver/bk4819.h"
#include "driver/system.h"
#include "radio/ax25.h"

#define FULL_WORDS   4u                  /* "almost full" interrupt every 4 words (8 bytes, 53 ms) */
#define REARM_MS     3000u               /* no packet end this long after a sync: start again */
#define FLIP_MS      1500u               /* no sync this long: try the other sync word */

static Ax25Rx_t    s_rx[4];              /* byte order x bit order */
static uint8_t     s_out[AX25_MAX_FRAME];
static FskRxStat_t s_st;
static bool        s_on, s_inv, s_in_packet;
static uint16_t    s_got;                /* words of this packet */
static uint32_t    s_t;                  /* SYSTEM_Ms of the last arming or sync */

static void arm(void)
{
	uint16_t w = s_inv ? 0x0101u : 0xFEFEu;
	BK4819_FskRxOn(s_st.mode, w, w, (uint8_t)FSKRX_LEN, FULL_WORDS);
	s_in_packet = false;
	s_got = 0;
	s_t = SYSTEM_Ms();
}

void FSKRX_Start(uint8_t mode)
{
	for (int k = 0; k < 4; k++)
		AX25_RxInit(&s_rx[k]);
	s_st.mode = mode & 1u;
	s_st.order = 0xFF;
	s_on = true;
	arm();
}

void FSKRX_Stop(void)
{
	if (s_on)
		BK4819_FskRxOff();
	s_on = false;
}

const FskRxStat_t *FSKRX_Stat(void)
{
	return &s_st;
}

/* one byte to receiver k, its bits high first (k bit 0 clear) or low first */
static unsigned feed(int k, uint8_t b)
{
	unsigned n = 0, got;
	for (int i = 0; i < 8; i++) {
		int bit = (k & 1) ? (b >> i & 1) : (b >> (7 - i) & 1);
		got = AX25_RxBit(&s_rx[k], bit != 0);
		if (got) {
			for (unsigned j = 0; j < got; j++)
				s_out[j] = s_rx[k].buf[j];
			n = got;
		}
	}
	return n;
}

static unsigned word(uint16_t w)
{
	unsigned n = 0, got;
	uint8_t hi = (uint8_t)(w >> 8), lo = (uint8_t)w;

	if (s_got < 4u)
		s_st.last[s_got] = w;
	s_got++;
	s_st.words++;
	for (int k = 0; k < 4; k++) {        /* k bit 1: low byte first */
		uint16_t bad = s_rx[k].bad_fcs;
		got = feed(k, (k & 2) ? lo : hi);
		if (!got)
			got = feed(k, (k & 2) ? hi : lo);
		if (s_rx[k].bad_fcs != bad)
			s_st.bad++;
		if (got) {
			n = got;
			s_st.order = (uint8_t)k;
			s_st.frames++;
		}
	}
	return n;
}

unsigned FSKRX_Poll(const uint8_t **frame)
{
	uint16_t f;
	unsigned n = 0, got;

	if (!s_on)
		return 0;
	if (!BK4819_FskRxArmed()) {          /* RADIO_Apply or the DTMF decoder took the chip back */
		arm();
		return 0;
	}
	f = BK4819_TakeFlags(BK4819_INT_FSK_RX);
	if (f & BK4819_INT_FSK_SYNC) {
		s_st.syncs++;
		s_in_packet = true;
		s_got = 0;
		s_t = SYSTEM_Ms();
		for (int k = 0; k < 4; k++)
			AX25_RxInit(&s_rx[k]);
	}
	if (f & BK4819_INT_FSK_FULL)
		for (unsigned i = 0; i < FULL_WORDS; i++)
			if ((got = word(BK4819_FskRead())) != 0)
				n = got;
	if (f & BK4819_INT_FSK_RX_DONE) {
		/* the rest of the packet still in the FIFO */
		while (s_got < (FSKRX_LEN + 1u) / 2u)
			if ((got = word(BK4819_FskRead())) != 0)
				n = got;
		s_st.done++;
		arm();
	} else if (SYSTEM_Ms() - s_t > (s_in_packet ? REARM_MS : FLIP_MS)) {
		if (!s_in_packet)
			s_inv = !s_inv;
		arm();
	}
	if (n)
		*frame = s_out;
	return n;
}
