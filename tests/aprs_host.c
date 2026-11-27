/* Host test for src/radio/ax25.c, aprs.c and afsk.c (run by tests/aprs_host.py):
 *   - the FCS check value, frames built and printed back, settings packed and unpacked;
 *   - heard packets described: plain, compressed and Mic-E positions (the APRS 1.01 examples), messages;
 *   - distance and bearing;
 *   - our beacon sent as Bell 202 audio (phase-continuous tones from the AX.25 bit stream), with noise and with the
 *     2200 Hz tone weaker (a receiver's de-emphasis), through the software demodulator, decoded back. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "radio/afsk.h"
#include "radio/aprs.h"
#include "radio/ax25.h"

static int fails;

#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static double gauss(void)
{
	double u = (rand() + 1.0) / (RAND_MAX + 2.0), v = (rand() + 1.0) / (RAND_MAX + 2.0);
	return sqrt(-2 * log(u)) * cos(2 * M_PI * v);
}

/* the frame as audio at fs, through the demodulator; returns how many times it was decoded back unchanged */
static int afsk_round(const uint8_t *frame, unsigned len, unsigned fs, double snr_db, double twist_db, int copies)
{
	static Afsk_t d;
	Ax25Tx_t t;
	double ph = 0, amp = 8000, a_space = amp * pow(10, -twist_db / 20), t_bit = 0, spb = fs / 1200.0;
	double sigma = amp / sqrt(2) / pow(10, snr_db / 20);
	int good = 0;

	AFSK_Init(&d, fs);
	for (int i = 0; i < (int)fs / 5; i++)        /* noise first */
		AFSK_Sample(&d, (int16_t)(gauss() * sigma));
	for (int c = 0; c < copies; c++) {
		int lv;
		AX25_TxStart(&t, frame, len, 20 + c * 3, 4);
		while ((lv = AX25_TxNext(&t)) >= 0) {
			double f = lv ? 1200.0 : 2200.0, a = lv ? amp : a_space;
			t_bit += spb;
			while (t_bit >= 1.0) {
				double x = a * sin(ph) + gauss() * sigma;
				unsigned n;
				ph += 2 * M_PI * f / fs;
				t_bit -= 1.0;
				if (x > 32767) x = 32767;
				if (x < -32768) x = -32768;
				n = AFSK_Sample(&d, (int16_t)x);
				if (n && n == len - 2u && !memcmp(d.ax.buf, frame, n))
					good++;
			}
		}
		for (int i = 0; i < (int)fs / 10; i++)
			AFSK_Sample(&d, (int16_t)(gauss() * sigma));
	}
	return good;
}

int main(void)
{
	uint8_t f[AX25_MAX_FRAME], packed[APRS_PACK_LEN];
	char text[400];
	unsigned n;
	AprsConf_t c, u;
	AprsHeard_t h;

	/* CRC-16/X-25 check value */
	CHECK(AX25_Fcs((const uint8_t *)"123456789", 9) == 0x906E, "fcs %04X", AX25_Fcs((const uint8_t *)"123456789", 9));

	/* our beacon */
	memset(&c, 0, sizeof c);
	strcpy(c.call, "KJ7ABC");
	c.ssid = 7;
	c.lat = 47 * 6000 + 39 * 100 + 50;           /* 47 39.50 N */
	c.lon = -(117 * 6000 + 24 * 100 + 30);       /* 117 24.30 W */
	c.icon = 1;
	c.every = 2;
	n = APRS_BuildBeacon(&c, f, sizeof f);
	CHECK(n > 0, "beacon not built");
	AX25_ToText(f, n - 2, text, sizeof text);
	printf("beacon: %s\n", text);
	CHECK(!strcmp(text, "KJ7ABC-7>APZUV5,WIDE1-1,WIDE2-1:!4739.50N/11724.30W>Open-UV5R-Mini"), "beacon text");
	CHECK(AX25_Fcs(f, n - 2) == (uint16_t)(f[n - 2] | f[n - 1] << 8), "beacon fcs");

	/* settings: packed and back (kept to 1/10 minute) */
	APRS_Pack(&c, packed);
	memset(&u, 0x55, sizeof u);
	APRS_Unpack(packed, &u);
	CHECK(!strcmp(u.call, "KJ7ABC") && u.ssid == 7 && u.lat == c.lat && u.lon == c.lon && u.icon == 1 && u.every == 2,
	      "unpack %s %u %d %d %u %u", u.call, u.ssid, (int)u.lat, (int)u.lon, u.icon, u.every);
	c.lat = -(33 * 6000 + 51 * 100 + 37);        /* 33 51.37 S: to 1/10 minute -> 33 51.40 */
	c.lon = 151 * 6000 + 12 * 100 + 94;
	APRS_Pack(&c, packed);
	APRS_Unpack(packed, &u);
	CHECK(u.lat == -(33 * 6000 + 5140) && u.lon == 151 * 6000 + 1290, "unpack south/east %d %d", (int)u.lat, (int)u.lon);
	memset(packed, 0, sizeof packed);
	APRS_Unpack(packed, &u);
	CHECK(!u.call[0] && u.lat == 0 && u.lon == 0, "unpack blank");
	CHECK(!APRS_BuildBeacon(&u, f, sizeof f), "beacon without a call sign");

	/* call signs */
	{
		char cs[7];
		uint8_t s;
		CHECK(AX25_ParseCall("n0call-15", cs, &s) && !strcmp(cs, "N0CALL") && s == 15, "parse call");
		CHECK(!AX25_ParseCall("N0CALL-16", cs, &s), "ssid 16");
		CHECK(!AX25_ParseCall("TOOLONG1", cs, &s), "7 letters");
		CHECK(!AX25_ParseCall("", cs, &s), "empty");
	}

	/* a frame with a digipeater that has repeated it */
	n = AX25_BuildUi(f, sizeof f, "APRS", "W1AW", "N7XYZ-3,WIDE2-1", (const uint8_t *)">hello", 6);
	f[2 * 7 + 6] |= 0x80;                         /* H bit of N7XYZ-3 (the third address) */
	AX25_ToText(f, n - 2, text, sizeof text);
	CHECK(!strcmp(text, "W1AW>APRS,N7XYZ-3*,WIDE2-1:>hello"), "digi text %s", text);

	/* heard: plain position */
	CHECK(APRS_Describe("N0CALL>APRS,WIDE2-1:!4903.50N/07201.75W-Test 001234", "KJ7ABC-7", &h), "describe");
	CHECK(!strcmp(h.from, "N0CALL") && !strcmp(h.kind, "POS") && h.has_pos && h.lat == 49 * 6000 + 350 &&
	      h.lon == -(72 * 6000 + 175) && h.sym == '-' && !strcmp(h.text, "Test 001234"),
	      "plain %s %s %d %d [%s]", h.from, h.kind, (int)h.lat, (int)h.lon, h.text);
	/* with a time stamp */
	CHECK(APRS_Describe("N0CALL>APRS:@092345z4903.50N/07201.75W>088/036", "", &h) && h.has_pos &&
	      h.lat == 49 * 6000 + 350 && h.sym == '>', "timed");
	/* compressed (APRS 1.01 chapter 9 example: 49 30' N 72 45' W) */
	CHECK(APRS_Describe("N0CALL>APRS:!/5L!!<*e7>7P[", "", &h) && h.has_pos, "compressed");
	CHECK(abs(h.lat - 49 * 6000 - 3000) <= 1 && abs(h.lon + 72 * 6000 + 4500) <= 1 && h.sym == '>',
	      "compressed %d %d %c", (int)h.lat, (int)h.lon, h.sym);
	/* Mic-E: S32UVT = 33 25.64 N, longitude +100, west; ( _ f = 12+100 degrees 07.74 */
	CHECK(APRS_Describe("N0CALL>S32UVT,WIDE1-1:`(_fn\"Oj/]comment", "", &h) && !strcmp(h.kind, "MIC-E"), "mic-e");
	CHECK(h.has_pos && h.lat == 33 * 6000 + 2564 && h.lon == -(112 * 6000 + 774) && h.sym == 'j' && h.sym_table == '/',
	      "mic-e %d %d %c%c", (int)h.lat, (int)h.lon, h.sym_table, h.sym);
	/* messages */
	CHECK(APRS_Describe("W1AW>APRS::KJ7ABC-7 :hi there{01", "KJ7ABC-7", &h) && !strcmp(h.kind, "MSG ME") &&
	      !strcmp(h.text, "hi there{01"), "msg me %s [%s]", h.kind, h.text);
	CHECK(APRS_Describe("W1AW>APRS::BLN1     :bulletin", "KJ7ABC-7", &h) && !strcmp(h.kind, "MSG"), "msg other");
	CHECK(APRS_Describe("W1AW>APRS:>on the air", "", &h) && !strcmp(h.kind, "STATUS") && !strcmp(h.text, "on the air"),
	      "status");
	CHECK(!APRS_Describe("garbage", "", &h), "garbage");

	/* distance: Spokane (47 39.5 N, 117 24.3 W) to Seattle (47 36.4 N, 122 19.9 W): about 369 km, bearing ~268 */
	{
		uint16_t b;
		uint32_t d = APRS_Distance(47 * 6000 + 3950, -(117 * 6000 + 2430), 47 * 6000 + 3640, -(122 * 6000 + 1990),
		                           false, &b);
		printf("distance %u.%u km bearing %u\n", (unsigned)(d / 10), (unsigned)(d % 10), b);
		CHECK(d > 3600 && d < 3760 && b >= 263 && b <= 273, "distance %u bearing %u", (unsigned)d, b);
		d = APRS_Distance(0, 0, 6000, 0, true, &b);      /* one degree north: 69 miles */
		CHECK(d >= 685 && d <= 695 && b == 0, "1 degree %u %u", (unsigned)d, b);
		d = APRS_Distance(0, 0, -6000, -6000, false, &b);
		CHECK(b >= 223 && b <= 227, "south-west bearing %u", b);
	}

	/* over the air: AFSK through the demodulator */
	c.lat = 47 * 6000 + 3950;
	c.lon = -(117 * 6000 + 2430);
	n = APRS_BuildBeacon(&c, f, sizeof f);
	{
		static const unsigned rates[] = { 9600, 13200, 22050, 44100 };
		for (unsigned r = 0; r < 4; r++) {
			int g = afsk_round(f, n, rates[r], 20, 0, 5);
			printf("afsk %5u Hz, 20 dB: %d/5\n", rates[r], g);
			CHECK(g == 5, "afsk %u clean", rates[r]);
		}
		{
			int g = afsk_round(f, n, 13200, 20, 6, 5);
			printf("afsk 13200 Hz, 20 dB, 2200 Hz 6 dB down: %d/5\n", g);
			CHECK(g >= 4, "afsk twist");
		}
		{
			int g = afsk_round(f, n, 13200, 10, 0, 10);
			printf("afsk 13200 Hz, 10 dB: %d/10\n", g);
			CHECK(g >= 7, "afsk 10 dB");
		}
	}

	printf(fails ? "FAIL (%d)\n" : "PASS\n", fails);
	return fails ? 1 : 0;
}
