/* PC test of app/settings.c (the stock VFO and settings logs). tests/settings_host.py builds and runs it.
 * With a path argument (a dump of a radio's external flash) it also reads that and prints what it found. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app/settings.h"

static uint8_t flash[0x10000];

void EXTFLASH_Read(uint32_t addr, uint8_t *buf, uint32_t len)
{
	while (len--)
		*buf++ = addr < sizeof flash ? flash[addr++] : 0xFF;
}

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static void vfo_rec(uint8_t *r, const char *mhz8, uint16_t rx, uint16_t tx, uint8_t power, int narrow, uint8_t step)
{
	memset(r, 0, 32);
	for (int i = 0; i < 8; i++)
		r[i] = (uint8_t)(mhz8[i] - '0');
	r[8] = (uint8_t)rx; r[9] = (uint8_t)(rx >> 8);
	r[10] = (uint8_t)tx; r[11] = (uint8_t)(tx >> 8);
	r[12] = 0xFF;
	r[14] = 0x40;                   /* shift - */
	r[16] = power;
	r[17] = narrow ? 0x40 : 0;
	r[19] = step;
	r[20] = 0; r[21] = 0; r[22] = 0; r[23] = 6;   /* 000.6000 MHz offset */
	memset(r + 28, 0xFF, 4);
}

static void put_vfo_slot(uint32_t a, const char *fa, const char *fb, int good)
{
	uint8_t *s = flash + a;
	uint16_t c;
	vfo_rec(s, fa, 885, 106, 1, 1, 4);
	vfo_rec(s + 32, fb, 0, 0, 0, 0, 7);
	c = SETTINGS_Crc16(s, 0x40);
	if (!good)
		c ^= 1;
	s[0x40] = (uint8_t)c;
	s[0x41] = (uint8_t)(c >> 8);
}

static void put_set_slot(uint32_t a, uint8_t squelch, int good)
{
	uint8_t *s = flash + a;
	uint16_t c;
	memset(s, 0, 0x60);
	s[SET_SQUELCH] = squelch;
	s[SET_DUAL_WATCH] = 0;
	s[SET_ACTIVE_SIDE] = 0;
	s[SET_WORK_MODE] = 0x00;
	c = SETTINGS_Crc16(s, 0x5E);
	if (!good)
		c ^= 1;
	s[0x5E] = (uint8_t)c;
	s[0x5F] = (uint8_t)(c >> 8);
}

int main(int argc, char **argv)
{
	/* nothing written: defaults */
	memset(flash, 0xFF, sizeof flash);
	CHECK(!SETTINGS_Load() && gStock.raw[SET_SQUELCH] == 3 && gStock.raw[SET_WORK_MODE] == 0x11, "empty: defaults");

	/* three slots each; the last counts */
	put_vfo_slot(0x8010, "14552000", "43562500", 1);
	put_vfo_slot(0x8052, "14600000", "43500000", 1);
	put_vfo_slot(0x8094, "14552500", "44600000", 1);
	put_set_slot(0x9010, 5, 1);
	put_set_slot(0x9070, 6, 1);
	put_set_slot(0x90D0, 7, 1);
	CHECK(SETTINGS_Load() && gStock.have_vfo && gStock.have_settings, "logs read");
	CHECK(gStock.vfo[0].freq == 14552500u && gStock.vfo[1].freq == 44600000u, "last VFO slot: %lu %lu",
	      (unsigned long)gStock.vfo[0].freq, (unsigned long)gStock.vfo[1].freq);
	CHECK(gStock.vfo[0].rx_tone == 885 && gStock.vfo[0].tx_tone == 106 && gStock.vfo[0].power == 1
	      && gStock.vfo[0].narrow && gStock.vfo[0].step == 4 && gStock.vfo[0].shift == -1
	      && gStock.vfo[0].offset == 60000u, "VFO A fields (offset %lu)", (unsigned long)gStock.vfo[0].offset);
	CHECK(gStock.vfo[1].step == 7 && !gStock.vfo[1].narrow && gStock.vfo[1].power == 0, "VFO B fields");
	CHECK(gStock.raw[SET_SQUELCH] == 7, "last settings slot: squelch %u", gStock.raw[SET_SQUELCH]);

	/* a bad CRC on the last slot: defaults, as stock */
	put_set_slot(0x9130, 8, 0);
	put_vfo_slot(0x80D6, "14400000", "43000000", 0);
	CHECK(!SETTINGS_Load() && !gStock.have_vfo && gStock.raw[SET_SQUELCH] == 3, "bad CRC: defaults");

	/* a frequency digit out of range: VFO refused */
	put_vfo_slot(0x80D6, "1445200:", "43000000", 1);
	SETTINGS_Load();
	CHECK(!gStock.have_vfo, "bad digit refused");

	if (argc > 1) {
		FILE *f = fopen(argv[1], "rb");
		if (f && fread(flash, 1, sizeof flash, f) == sizeof flash) {
			bool ok = SETTINGS_Load();
			printf("dump: settings %s, VFOs %s; squelch %u, dual watch %u, active side %c, work mode %02X\n",
			       gStock.have_settings ? "ok" : "none", gStock.have_vfo ? "ok" : "none",
			       gStock.raw[SET_SQUELCH], gStock.raw[SET_DUAL_WATCH], 'A' + gStock.raw[SET_ACTIVE_SIDE],
			       gStock.raw[SET_WORK_MODE]);
			for (int i = 0; i < 2 && gStock.have_vfo; i++)
				printf("dump: VFO %c %lu.%05lu MHz, tones %u/%u, power %u, %s, step %u\n", 'A' + i,
				       (unsigned long)(gStock.vfo[i].freq / 100000u), (unsigned long)(gStock.vfo[i].freq % 100000u),
				       gStock.vfo[i].rx_tone, gStock.vfo[i].tx_tone, gStock.vfo[i].power,
				       gStock.vfo[i].narrow ? "narrow" : "wide", gStock.vfo[i].step);
			CHECK(ok, "the dump's logs read");
		}
		if (f)
			fclose(f);
	}

	printf(fails ? "settings_host: %d FAILED\n" : "settings_host: all passed\n", fails);
	return fails != 0;
}
