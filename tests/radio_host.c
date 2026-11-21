/* PC test of app/radio.c with stand-in drivers (tests/radio_host.py builds it twice: transmit off and on).
 * Checks the transmit guard (amateur bands only, never airband or AM, never with RADIO_ENABLE_TX 0), the band switch
 * outputs, and the squelch opening and closing on the RSSI. */
#include <stdio.h>
#include <string.h>
#include "app/radio.h"
#include "app/calib.h"
#include "app/channels.h"
#include "board.h"
#include "driver/bk4819.h"
#include "driver/io.h"

bool test_pins[128];
static uint16_t rssi = 100, reg33, reg30, reg36;
static int amp, led_rx, led_tx;
static BK4819_AF_t af;

uint16_t BK4819_GetRSSI(void) { return rssi; }
static uint16_t reg78;
void BK4819_WriteRegister(uint8_t r, uint16_t v) { if (r == 0x78) reg78 = v; }
void EXTFLASH_Read(uint32_t addr, uint8_t *buf, uint32_t len) { (void)addr; while (len--) *buf++ = 0xFF; }
void BK4819_SetFrequency(uint32_t f) { (void)f; }
void BK4819_SetFilterBandwidth(bool n) { (void)n; }
void BK4819_SetVolume(uint8_t v) { (void)v; }
void BK4819_SetGpio(uint8_t m, bool on) { reg33 = (uint16_t)(on ? (reg33 | m) : (reg33 & ~m)); }
void BK4819_SetPa(uint8_t bias, uint8_t gain) { reg36 = bias ? (uint16_t)(bias << 8 | gain) : 0x7F; }
void BK4819_SetTxCtcss(uint16_t c) { (void)c; }
void BK4819_RxOn(void) { reg30 = 0xBFF1; }
void BK4819_TxOn(void) { reg30 = 0xC1FE; }
void BK4819_TxMute(bool on) { (void)on; }
void BK4819_SetAF(BK4819_AF_t a) { af = a; }
void BK4819_Beep(uint16_t hz) { (void)hz; }
void IO_Init(void) {}
void IO_Amp(uint8_t who, bool on) { (void)who; amp = on; }
void IO_LedRx(bool on) { led_rx = on; }
void IO_LedTx(bool on) { led_tx = on; }

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static void at(uint32_t f) { gVfo.freq = f; RADIO_Apply(); }
static void ticks(int n) { while (n--) RADIO_Tick(); }

/* A made-up calibration with the shape of a real one (no radio's own data is kept in the repo). */
static uint8_t cal[CALIB_LEN];
static void make_cal(void)
{
	memset(cal, 0xFF, sizeof cal);
	for (int i = 0; i < 16; i++) {
		cal[0x000 + i] = 0xD2; cal[0x010 + i] = 0xD7; cal[0x020 + i] = 0x82; cal[0x030 + i] = 0;   /* High */
		cal[0x080 + i] = 0x96; cal[0x090 + i] = 0x95; cal[0x0A0 + i] = 0x96; cal[0x0B0 + i] = 0;   /* Low */
		cal[0x040 + i] = 0x91; cal[0x050 + i] = 0x82; cal[0x060 + i] = 0x64; cal[0x070 + i] = 0;   /* level 2 */
		cal[0x0E0 + i] = (uint8_t)(0x13 + i);   /* UHF squelch offsets */
		cal[0x0F0 + i] = (uint8_t)i;            /* VHF */
		cal[0x100 + i] = 0x1E;
		cal[0x110 + i] = 0x1E;
	}
	for (int l = 0; l < 10; l++) {
		cal[0xC0 + l] = l ? (uint8_t)(0x69 + l) : 0;
		cal[0xD0 + l] = l ? (uint8_t)(0x68 + l) : 0;
	}
}

int main(void)
{
	RADIO_Init();

	/* band switch outputs on receive */
	at(14552000u);
	CHECK(test_pins[PIN_RX_FRONT] && !(reg33 & BK4819_GPIO_RX_UHF), "VHF receive front end");
	at(44600000u);
	CHECK(!test_pins[PIN_RX_FRONT] && (reg33 & BK4819_GPIO_RX_UHF), "UHF receive front end");
	CHECK(!(reg33 & (BK4819_GPIO_TX_VHF | BK4819_GPIO_TX_UHF)), "no TX path on receive");

	/* squelch: closed on noise, opens on a strong signal, closes again */
	gVfo.squelch = 3;
	at(14552000u);
	rssi = 60;
	ticks(20);
	CHECK(RADIO_State() == RADIO_RX && !amp && af == BK4819_AF_MUTE, "closed on noise");
	rssi = 200;
	ticks(5);
	CHECK(RADIO_State() == RADIO_RX_OPEN && amp && led_rx && af == BK4819_AF_FM, "opens on signal");
	rssi = 60;
	ticks(30);
	CHECK(RADIO_State() == RADIO_RX && !amp, "closes again");
	gVfo.squelch = 0;
	at(14552000u);
	CHECK(RADIO_State() == RADIO_RX_OPEN && amp, "squelch 0 is open");
	at(12000000u);
	ticks(2);
	CHECK(af == BK4819_AF_AM, "airband is AM");
	gVfo.squelch = 3;

	/* no calibration: never transmit */
	at(14552000u);
	CHECK(!RADIO_StartTx() && reg36 == 0x7F, "no TX without calibration");

	/* calibration: PA bias, squelch thresholds */
	make_cal();
	CALIB_Set(cal);
	CHECK(CALIB_PaBias(14552000u, POWER_LOW) == 0x95 && CALIB_PaBias(14552000u, POWER_HIGH) == 0xD7, "VHF PA rows");
	CHECK(CALIB_PaBias(44600000u, POWER_LOW) == 0x96 && CALIB_PaBias(44600000u, POWER_HIGH) == 0xD2, "UHF PA rows");
	CHECK(CALIB_PaBias(35500000u, POWER_HIGH) == 0, "350 MHz band uncalibrated");
	CHECK(CALIB_PaGain(POWER_LOW) == 0xD7 && CALIB_PaGain(POWER_HIGH) == 0xFF, "gain bytes");
	{
		uint8_t o, c;
		CALIB_Squelch(14552000u, 3, &o, &c);    /* VHF offset index (145 - 130) / 5 = 3 */
		CHECK(o == 0x6C - 3 && c == 0x6B - 3, "VHF squelch 3: %u %u", o, c);
		CALIB_Squelch(44600000u, 1, &o, &c);    /* UHF offset index 4 */
		CHECK(o == 0x6A - 0x17 && c == 0x69 - 0x17, "UHF squelch 1: %u %u", o, c);
		CALIB_Squelch(12000000u, 1, &o, &c);    /* airband: level 9 entry, close = open - 3 */
		CHECK(o == 0x72 && c == 0x72 - 3, "airband squelch: %u %u", o, c);
		gVfo.squelch = 3;
		at(14552000u);
		CHECK(reg78 == (uint16_t)((0x6C - 3) << 8 | (0x6B - 3)), "REG_78 written: %04X", reg78);
		rssi = 0x6C - 3;
		ticks(4);
		CHECK(RADIO_State() == RADIO_RX_OPEN, "opens at the calibrated level");
		rssi = 0x6B - 4;
		ticks(20);
		CHECK(RADIO_State() == RADIO_RX, "closes below the calibrated close level");
	}

	/* transmit guard */
	gVfo.power = POWER_LOW;
	{
		static const struct { uint32_t f; int ok; } T[] = {
			{ 14400000u, 1 }, { 14552000u, 1 }, { 14799999u, 1 }, { 14800000u, 0 }, { 14399999u, 0 },
			{ 42000000u, 1 }, { 44600000u, 1 }, { 45000000u, 0 }, { 46256250u, 0 }, { 12000000u, 0 },
			{ 15600000u, 0 }, { 22300000u, 0 }, { 35000000u, 0 },
		};
		for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
			at(T[i].f);
			int want = RADIO_ENABLE_TX && T[i].ok;
			bool on = RADIO_StartTx();
			CHECK(on == want, "TX at %lu: %d, want %d", (unsigned long)T[i].f, on, want);
			if (on) {
				CHECK(reg30 == 0xC1FE && led_tx && reg36 == (uint16_t)(CALIB_PaBias(T[i].f, POWER_LOW) << 8 | 0xD7) && !amp,
				      "TX on at %lu: REG_36 %04X", (unsigned long)T[i].f, reg36);
				CHECK((T[i].f < 20000000u) == !!(reg33 & BK4819_GPIO_TX_VHF), "TX path at %lu", (unsigned long)T[i].f);
				RADIO_EndTx();
				CHECK(reg30 == 0xBFF1 && !led_tx && reg36 == 0x7F && !(reg33 & 0x18), "back to RX");
			} else
				CHECK(reg30 != 0xC1FE && reg36 == 0x7F, "stayed in RX at %lu", (unsigned long)T[i].f);
		}
		gVfo.mod = MOD_AM;
		at(14552000u);
		CHECK(!RADIO_StartTx(), "never TX in AM");
		gVfo.mod = MOD_AUTO;

		/* a channel: TX on its own frequency, and a receive-only channel never transmits */
		gVfo.freq = 14600000u;   /* RX outside the TX bands */
		gVfo.tx_freq = 14552000u;
		RADIO_Apply();
		CHECK(RADIO_StartTx() == RADIO_ENABLE_TX, "TX on the channel's TX frequency");
		RADIO_EndTx();
		gVfo.freq = 14552000u;
		gVfo.tx_freq = 15600000u;  /* TX outside */
		RADIO_Apply();
		CHECK(!RADIO_StartTx(), "no TX when the channel's TX frequency is outside");
		gVfo.tx_freq = 0;
		gVfo.rx_only = true;
		RADIO_Apply();
		CHECK(!RADIO_StartTx(), "no TX on a receive-only channel");
		gVfo.rx_only = false;
	}

	/* channel records */
	{
		static const uint8_t rec[32] = {
			0x50, 0x62, 0x25, 0x46, 0x50, 0x62, 0x25, 0x46, 0x00, 0x00, 0x75, 0x03, 0x00, 0x00, 0x01, 0x44,
			0xFF, 0xFF, 0xFF, 0xFF, 'F', 'R', 'S', '1', 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		};
		uint8_t r2[32];
		Channel_t ch;
		CHECK(CHANNEL_Decode(rec, &ch) && ch.rx == 46256250u && ch.tx == 46256250u, "BCD frequencies");
		CHECK(ch.tx_ctcss == 885 && ch.rx_ctcss == 0, "tones %u %u", ch.tx_ctcss, ch.rx_ctcss);
		CHECK(ch.narrow && ch.scan && ch.low_power && !strcmp(ch.name, "FRS1"), "flags and name '%s'", ch.name);
		memcpy(r2, rec, 32);
		memset(r2 + 4, 0xFF, 4);
		CHECK(CHANNEL_Decode(r2, &ch) && ch.tx == 0, "receive only");
		r2[0] = 0xFF;
		CHECK(!CHANNEL_Decode(r2, &ch), "empty slot");
		memcpy(r2, rec, 32);
		r2[1] = 0xA2;
		CHECK(!CHANNEL_Decode(r2, &ch), "bad BCD refused");
	}

	printf(fails ? "radio_host (TX %d): %d FAILED\n" : "radio_host (TX %d): all passed\n", RADIO_ENABLE_TX, fails);
	return fails != 0;
}
