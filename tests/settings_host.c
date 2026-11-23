/* PC test of store/settings.c: reading the stock VFO and settings logs, and (built with SETTINGS_ENABLE_SAVE 1 here)
 * writing them the stock way, on a simulated NOR flash: programming can only clear bits, erase sets a 4 KB sector to
 * 0xFF, and any erase or program outside 0x8000-0x9FFF counts as a failure.
 * tests/settings_host.py builds and runs it. With a path argument (a dump of a radio's external flash) it also
 * checks the reader and the writer against that. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "store/settings.h"

#define FLASH_SIZE 0x10000u
static uint8_t flash[FLASH_SIZE];
static int outside, programs, erases, fail_next;

void EXTFLASH_Read(uint32_t addr, uint8_t *buf, uint32_t len)
{
	while (len--)
		*buf++ = addr < FLASH_SIZE ? flash[addr++] : 0xFF;
}

int EXTFLASH_Erase4K(uint32_t addr)
{
	if ((addr & 0xFFFu) || addr < 0x8000u || addr >= 0xA000u) {
		outside++;
		return -1;
	}
	erases++;
	memset(flash + addr, 0xFF, 0x1000);
	return 0;
}

int EXTFLASH_Program(uint32_t addr, const uint8_t *buf, uint32_t len)
{
	if (addr < 0x8000u || addr + len > 0xA000u) {
		outside++;
		return -1;
	}
	if (fail_next) {
		fail_next = 0;
		return -1;
	}
	programs++;
	for (uint32_t i = 0; i < len; i++)
		flash[addr + i] &= buf[i];
	for (uint32_t i = 0; i < len; i++)
		if (flash[addr + i] != buf[i])
			return -1;                         /* read back, as the driver does */
	return 0;
}

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static void set_vfo(int side, uint32_t f, uint16_t rx, uint16_t tx, uint8_t power, int narrow, uint8_t step)
{
	StockVfo_t *v = &gStock.vfo[side];
	v->freq = f; v->rx_tone = rx; v->tx_tone = tx; v->power = power; v->narrow = narrow; v->step = step;
	v->shift = side ? 0 : -1;
	v->offset = side ? 0 : 60000u;
}

static int dump_checks(const char *path)
{
	static uint8_t orig[FLASH_SIZE];
	FILE *f = fopen(path, "rb");
	int n_vfo = 0, same = 0;

	if (!f)
		return 0;
	if (fread(flash, 1, FLASH_SIZE, f) != FLASH_SIZE) {
		fclose(f);
		return 0;
	}
	fclose(f);
	memcpy(orig, flash, FLASH_SIZE);

	CHECK(SETTINGS_Load() && gStock.have_settings && gStock.have_vfo, "the dump's logs read");
	CHECK(gStock.ani[7] < 6 && gStock.ani[8] < 6 && gStock.ani[9] < 6 && gStock.ani[10] < 7 &&
	      (gStock.up_code[0] < 16 || gStock.up_code[0] == 0xFF) && (gStock.down_code[0] < 16 || gStock.down_code[0] == 0xFF),
	      "dump: the ANI block and up / down codes are in the stock ranges");
	printf("dump: squelch %u, dual watch %u, active side %c, work mode %02X; A %lu, B %lu (10 Hz)\n",
	       gStock.raw[SET_SQUELCH], gStock.raw[SET_DUAL_WATCH], 'A' + gStock.raw[SET_ACTIVE_SIDE],
	       gStock.raw[SET_WORK_MODE], (unsigned long)gStock.vfo[0].freq, (unsigned long)gStock.vfo[1].freq);

	/* every VFO record ever written: decode then encode gives the same bytes */
	for (uint32_t a = SETTINGS_VFO_LOG; a + SETTINGS_VFO_SLOT <= 0x9000u; a += SETTINGS_VFO_SLOT) {
		uint8_t rec[64], again[64];
		StockVfo_t v;
		memcpy(rec, flash + a, 64);
		if (rec[0] == 0xFF)
			break;
		for (int s = 0; s < 2; s++) {
			n_vfo++;
			memcpy(again, rec, 64);
			if (SETTINGS_DecodeVfo(rec + 32 * s, &v)) {
				SETTINGS_EncodeVfo(again + 32 * s, &v);
				if (memcmp(again, rec, 64)) {
					for (int k = 0; k < 64; k++)
						if (again[k] != rec[k])
							printf("dump VFO 0x%04lX side %d byte %d: %02X -> %02X\n", (unsigned long)a, s, k, rec[k], again[k]);
				} else
					same++;
			}
		}
	}
	CHECK(n_vfo && same == n_vfo, "dump: %d of %d VFO records encode back unchanged", same, n_vfo);

	/* a save on the dump: one new slot and one header byte, nothing else */
	gStock.raw[SET_SQUELCH] = (uint8_t)(gStock.raw[SET_SQUELCH] == 5 ? 4 : 5);
	{
		uint8_t want = gStock.raw[SET_SQUELCH];
		int changed = 0, lo = -1, hi = -1;
		CHECK(SETTINGS_SaveSettings() == 0, "dump: settings saved");
		for (uint32_t i = 0; i < FLASH_SIZE; i++)
			if (flash[i] != orig[i]) {
				changed++;
				if (lo < 0)
					lo = (int)i;
				hi = (int)i;
			}
		printf("dump: save changed %d bytes, 0x%04X-0x%04X\n", changed, lo, hi);
		CHECK(lo >= 0x9000 && hi < 0xA000, "dump: only the settings sector changed");
		CHECK(flash[0x9004] == (orig[0x9004] & 0xFE) && orig[0x9004] == 0xFF, "dump: header bit of slot 32 cleared");
		SETTINGS_Load();
		CHECK(gStock.raw[SET_SQUELCH] == want, "dump: the new squelch reads back");
		CHECK(!memcmp(flash, orig, 0x9000) && !memcmp(flash + 0xA000, orig + 0xA000, FLASH_SIZE - 0xA000),
		      "dump: channels, VFOs and calibration untouched");
	}
	return 1;
}

int main(int argc, char **argv)
{
	/* nothing written: defaults */
	memset(flash, 0xFF, sizeof flash);
	CHECK(!SETTINGS_Load() && gStock.raw[SET_SQUELCH] == 3 && gStock.raw[SET_WORK_MODE] == 0x11, "blank: defaults");

	/* encode / decode */
	{
		uint8_t rec[32];
		StockVfo_t v;
		memset(rec, 0, sizeof rec);
		set_vfo(0, 14552500u, 885, 106, 1, 1, 4);
		SETTINGS_EncodeVfo(rec, &gStock.vfo[0]);
		CHECK(SETTINGS_DecodeVfo(rec, &v) && v.freq == 14552500u && v.rx_tone == 885 && v.tx_tone == 106 && v.power == 1
		      && v.narrow && v.step == 4 && v.shift == -1 && v.offset == 60000u && rec[18] == 0, "VFO record round trip");
		set_vfo(0, 12000000u, 0, 0, 0, 0, 0);
		SETTINGS_EncodeVfo(rec, &gStock.vfo[0]);
		CHECK(rec[18] == 4, "airband: band byte 4");
	}

	/* saving on a blank log: slot 1 written, header bit 0 cleared (as stock: slot 0 is the blank current one) */
	SETTINGS_Defaults();
	gStock.raw[SET_SQUELCH] = 6;
	gStock.ext[0] = 0xA5;                /* our bytes (channel numbers etc.) at slot 0x40 */
	gStock.ext[2] = 0xE6;
	gStock.ext[3] = 0x03;
	gStock.ext[15] = 0x5C;
	set_vfo(0, 14552500u, 885, 0, 0, 0, 4);
	set_vfo(1, 43562500u, 0, 106, 1, 1, 7);
	memset(gStock.vfo_raw, 0, sizeof gStock.vfo_raw);
	CHECK(SETTINGS_SaveSettings() == 0 && SETTINGS_SaveVfos() == 0, "saved");
	CHECK(flash[0x9000] == 0xFE && flash[0x8000] == 0xFE, "header bit 0 cleared");
	CHECK(flash[0x9070 + 0x40] == 0xA5 && flash[0x9070 + 0x4F] == 0x5C && flash[0x9070 + 0x50] == 0,
	      "our bytes at slot 0x40-0x4F, 0x50 on still 0");
	memset(gStock.ext, 0, sizeof gStock.ext);
	CHECK(SETTINGS_Load() && gStock.raw[SET_SQUELCH] == 6 && gStock.vfo[0].freq == 14552500u
	      && gStock.vfo[1].freq == 43562500u && gStock.vfo[1].tx_tone == 106, "saved values read back");
	CHECK(gStock.ext[0] == 0xA5 && gStock.ext[2] == 0xE6 && gStock.ext[3] == 0x03 && gStock.ext[15] == 0x5C,
	      "our bytes read back");

	/* the same again: nothing written */
	programs = 0;
	SETTINGS_SaveSettings();
	SETTINGS_SaveVfos();
	CHECK(programs == 0, "unchanged: no write (%d)", programs);

	/* many saves: the log wraps with one sector erase each, and the last value always reads back */
	{
		int bad = 0;
		erases = 0;
		for (int i = 0; i < 130; i++) {
			gStock.raw[SET_SQUELCH] = (uint8_t)(i % 10);
			gStock.vfo[0].freq = 14400000u + (uint32_t)i * 1250u;
			if (SETTINGS_SaveSettings() || SETTINGS_SaveVfos()) {
				bad++;
				break;
			}
			SETTINGS_Load();
			if (gStock.raw[SET_SQUELCH] != i % 10 || gStock.vfo[0].freq != 14400000u + (uint32_t)i * 1250u) {
				if (!bad)
					printf("save %d: squelch %u freq %lu\n", i, gStock.raw[SET_SQUELCH], (unsigned long)gStock.vfo[0].freq);
				bad++;
			}
		}
		CHECK(!bad, "130 saves read back");
		CHECK(erases == 3 + 2, "erases: settings every 41 slots, VFOs every 60 (%d)", erases);
	}

	/* a failed write: reported, and the previous values still read */
	gStock.raw[SET_SQUELCH] = 9;
	SETTINGS_SaveSettings();
	gStock.raw[SET_SQUELCH] = 2;
	fail_next = 1;
	CHECK(SETTINGS_SaveSettings() == -1, "failed write reported");
	SETTINGS_Load();
	CHECK(gStock.raw[SET_SQUELCH] == 9, "after a failed write the last good value reads");

	/* a bad CRC on the current slot: defaults, as stock */
	{
		uint32_t i = 0;
		while (flash[0x9000 + (i >> 3)] >> (i & 7) & 1 ? 0 : 1)
			i++;
		flash[0x9010 + i * 0x60] ^= 0x01;
		CHECK(!SETTINGS_Load() || !gStock.have_settings, "bad CRC: settings default");
	}

	CHECK(outside == 0, "no erase or program outside 0x8000-0x9FFF (%d)", outside);

	if (argc > 1 && !dump_checks(argv[1]))
		printf("(dump %s not read)\n", argv[1]);

	printf(fails ? "settings_host: %d FAILED\n" : "settings_host: all passed\n", fails);
	return fails != 0;
}
