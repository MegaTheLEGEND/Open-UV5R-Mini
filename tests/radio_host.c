/* PC test of radio/radio.c with stand-in drivers (tests/radio_host.py builds it twice: transmit off and on).
 * Checks the transmit guard (amateur bands only, never airband or AM, never with RADIO_ENABLE_TX 0), the band switch
 * outputs, and the squelch opening and closing on the RSSI. */
#include <stdio.h>
#include <string.h>
#include "radio/radio.h"
#include "app/battery.h"
#include "store/calib.h"
#include "store/channels.h"
#include "radio/dualwatch.h"
#include "radio/dtmfseq.h"
#include "radio/scan.h"
#include "radio/vox.h"
#include "radio/tones.h"
#include "radio/txlock.h"
#include "board.h"
#include "driver/bk4819.h"
#include "driver/io.h"

bool test_pins[128];
static uint16_t rssi = 100, reg33, reg30, reg36;
static int amp, led_rx, led_tx;
static BK4819_AF_t af;

uint16_t BK4819_GetRSSI(void) { return rssi; }
static uint16_t reg78;
/* the chip's squelch as REG_0C bit 1: opens at REG_78's high byte, closes below its low byte */
static bool chip_sq;
bool BK4819_SquelchOpen(void)
{
	if (chip_sq ? rssi < (reg78 & 0xFFu) : rssi >= (reg78 >> 8))
		chip_sq = !chip_sq;
	return chip_sq;
}
void BK4819_WriteRegister(uint8_t r, uint16_t v) { if (r == 0x78) reg78 = v; }
void EXTFLASH_Read(uint32_t addr, uint8_t *buf, uint32_t len) { (void)addr; while (len--) *buf++ = 0xFF; }
int EXTFLASH_Erase4K(uint32_t addr) { (void)addr; return -1; }
int EXTFLASH_Program(uint32_t addr, const uint8_t *buf, uint32_t len) { (void)addr; (void)buf; (void)len; return -1; }
void BK4819_SetFrequency(uint32_t f) { (void)f; }
void BK4819_SetFilterBandwidth(bool n) { (void)n; }
void BK4819_SetVolume(uint8_t v) { (void)v; }
void BK4819_SetGpio(uint8_t m, bool on) { reg33 = (uint16_t)(on ? (reg33 | m) : (reg33 & ~m)); }
void BK4819_SetPa(uint8_t bias, uint8_t gain) { reg36 = bias ? (uint16_t)(bias << 8 | gain) : 0x7F; }
static uint16_t css_tenths, css_heard_mask;
static uint32_t css_word;
static int css_mode;     /* 0 off, 1 CTCSS, 2 DCS N, 3 DCS I */
void BK4819_SetCtcss(uint16_t t, uint8_t l) { (void)l; css_mode = 1; css_tenths = t; }
void BK4819_SetDcs(uint32_t w, bool inv, uint8_t l) { (void)l; css_mode = inv ? 3 : 2; css_word = w; }
void BK4819_CssOff(void) { css_mode = 0; }
static int dtmf_on, dtmf_code = -1;
void BK4819_DtmfRxOn(void) { dtmf_on = 1; }
void BK4819_DtmfRxOff(void) { dtmf_on = 0; }
int BK4819_DtmfTake(void) { int c = dtmf_code; dtmf_code = -1; return c; }
bool BK4819_CssHeard(BK4819_Tone_t k) { return (css_heard_mask >> k) & 1u; }
void BK4819_RxOn(void) { reg30 = 0xBFF1; }
void BK4819_Sleep(void) { reg30 = 0; }
void SYSTEM_DelayUs(uint32_t us) { (void)us; }
uint16_t BK4819_ReadRegister(uint8_t r) { (void)r; return 0; }
void BK4819_TxOn(void) { reg30 = 0xC1FE; }
void BK4819_TxMute(bool on) { (void)on; }
static uint8_t mic_gain = 0xFF;
void BK4819_SetMicGain(uint8_t g) { mic_gain = g; }
void BK4819_SetAF(BK4819_AF_t a) { af = a; }
/* tones sent while transmitting: the pairs written, in order */
static int tx_tones_on, tx_mon, tx_log_n;
static uint16_t tx_w1, tx_w2, tx_log[8][2];
static uint32_t delay_ms;
uint16_t BK4819_ToneWord(uint16_t hz) { return (uint16_t)(((uint32_t)hz * 1032444u + 50000u) / 100000u); }
void BK4819_TxTonesOn(bool dual) { tx_tones_on = dual ? 2 : 1; }
void BK4819_TxTonePair(uint16_t a, uint16_t b)
{
	tx_w1 = a; tx_w2 = b;
	if (tx_log_n < 8) { tx_log[tx_log_n][0] = a; tx_log[tx_log_n][1] = b; tx_log_n++; }
}
void BK4819_TxTonesOff(void) { tx_tones_on = 0; tx_w1 = tx_w2 = 0; }
void BK4819_TxMonitor(bool on) { tx_mon = on; reg30 = on ? 0xC3FA : 0xC1FE; }
void SYSTEM_DelayMs(uint32_t ms) { delay_ms += ms; }
static uint32_t raw_clock;
uint32_t SYSTEM_Raw(void) { return raw_clock += 50u; }     /* RADIO_SendAfsk waits on it */
uint32_t SYSTEM_TicksPerSec(void) { return 375000u; }
void SYSTEM_WatchdogClear(void) {}
static int tail_mode = -1;     /* -1 none, 0 55 Hz, 1 DCS */
void BK4819_TailTone(bool dcs, uint8_t level) { (void)level; tail_mode = dcs; }
void BK4819_Beep(uint16_t hz) { if (hz) reg30 = 0x0302; }
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
	/* sleep mode (app.c sleep_tick): the receiver asleep hears nothing; RADIO_Apply wakes it */
	rssi = 200;
	RADIO_RxSleep();
	ticks(10);
	CHECK(RADIO_RxAsleep() && reg30 == 0 && RADIO_State() == RADIO_RX && !amp && !RADIO_Carrier(),
	      "asleep: receiver off, hears nothing");
	at(14552000u);
	ticks(5);
	CHECK(!RADIO_RxAsleep() && reg30 == 0xBFF1 && RADIO_State() == RADIO_RX_OPEN, "awake again, opens on signal");
	rssi = 60;
	ticks(30);
	/* parked (broadcast FM in the VFO, app.c): asleep, and RADIO_Apply, monitor, squelch 0 and TX leave it so */
	RADIO_Park(true);
	rssi = 200;
	at(10070000u);
	RADIO_Monitor(true);
	ticks(10);
	CHECK(RADIO_Parked() && RADIO_RxAsleep() && reg30 == 0 && !amp && af == BK4819_AF_MUTE && gVfo.freq == 10070000u,
	      "parked: retune and monitor keep the BK4819 asleep and muted");
	CHECK(!RADIO_StartTx() && RADIO_TxRefusal() && RADIO_State() != RADIO_TX, "parked: no transmit");
	RADIO_Monitor(false);
	RADIO_Park(false);
	at(14552000u);
	ticks(5);
	CHECK(!RADIO_Parked() && !RADIO_RxAsleep() && reg30 == 0xBFF1 && RADIO_State() == RADIO_RX_OPEN,
	      "unparked: awake on the VFO, opens on signal");
	rssi = 60;
	ticks(30);
	/* spectrum probe (app/spectrum.c): audio off and the squelch idle while the receiver is moved and read; the
	 * end tunes back to the VFO and the squelch works again */
	RADIO_ProbeBegin();
	rssi = 200;
	{
		uint16_t r = RADIO_ProbeRssi(43950000u, 100);
		ticks(10);
		CHECK(r == 200 && !amp && RADIO_State() == RADIO_RX && reg30 == 0xBFF1, "probe reads RSSI, stays muted");
		CHECK(test_pins[PIN_RX_FRONT] == 0 && (reg33 & BK4819_GPIO_RX_UHF), "probe sets the UHF front end");
	}
	RADIO_ProbeEnd();
	ticks(5);
	CHECK(RADIO_State() == RADIO_RX_OPEN && amp && test_pins[PIN_RX_FRONT], "after the probe: back on the VFO, open");
	rssi = 60;
	ticks(30);
	gVfo.squelch = 0;
	at(14552000u);
	CHECK(RADIO_State() == RADIO_RX_OPEN && amp, "squelch 0 is open");
	at(12000000u);
	ticks(2);
	CHECK(af == BK4819_AF_AM, "airband is AM");
	gVfo.squelch = 3;

	/* tones: the stock encoding, the DCS word, tone squelch */
	{
		char b[8];
		CHECK(TONE_Golay(023) == 0x763813u, "D023 word %06lX", (unsigned long)TONE_Golay(023));
		CHECK(TONE_Kind(1) == TONE_DCS_N && TONE_DcsCode(1) == 023, "index 1 = D023N");
		CHECK(TONE_Kind(106) == TONE_DCS_I && TONE_DcsCode(106) == 023, "index 106 = D023I");
		CHECK(TONE_DcsCode(94) == 0645, "index 94 = D645");
		CHECK(!strcmp(TONE_Format(b, 106), "D023I") && !strcmp(TONE_Format(b, 885), "88.5")
		      && !strcmp(TONE_Format(b, 0xFFFF), "OFF"), "formats");
		CHECK(TONE_Kind(0xFFFF) == TONE_NONE && TONE_Kind(300) == TONE_NONE && TONE_Normalise(250) == 0, "none");
		CHECK(TONE_Step(0, 1) == 630 && TONE_Step(630, -1) == 0 && TONE_Step(2541, 1) == 1
		      && TONE_Step(105, 1) == 106 && TONE_Step(210, 1) == 0 && TONE_Step(0, -1) == 210, "tone steps");
		for (unsigned i = 0; i < TONE_DCS_COUNT; i++) {
			uint32_t w = TONE_Golay(TONE_DcsCodes[i]), r = w & 0xFFFu;   /* data in bits 0-11, check bits 12-22 */
			uint32_t v = r << 11 | (w >> 12);                               /* as a polynomial: data * x^11 + check */
			for (int bit = 22; bit >= 11; bit--)
				if (v & (1u << bit))
					v ^= 0xC75u << (bit - 11);
			if (v) {
				CHECK(0, "DCS %03o word not a code word", TONE_DcsCodes[i]);
				break;
			}
		}

		/* tone scan lookups */
		{
			uint32_t w = TONE_Golay(023), r = w;
			for (int i = 0; i < 5; i++)
				r = (r << 1 & 0x7FFFFFu) | (r >> 22 & 1u);
			CHECK(TONE_FromCtcss(886) == 885 && TONE_FromCtcss(894) == 885 && TONE_FromCtcss(900) == 0
			      && TONE_FromCtcss(630) == 630 && TONE_FromCtcss(500) == 0, "nearest CTCSS within 0.9 Hz");
			CHECK(TONE_FromDcsWord(w) == 1 && TONE_FromDcsWord(r) == 1, "D023 word, and rotated");
			/* inverted D023 is normal D047 rotated (a known DCS alias): normal codes are checked first, as stock */
			CHECK(TONE_DcsCode(TONE_FromDcsWord(r ^ 0x7FFFFFu)) == 047 && TONE_Kind(TONE_FromDcsWord(r ^ 0x7FFFFFu)) == TONE_DCS_N,
			      "inverted D023 found as D047N (%u)", TONE_FromDcsWord(r ^ 0x7FFFFFu));
			CHECK(TONE_FromDcsWord(0x123456) == 0 || TONE_Kind(TONE_FromDcsWord(0x123456)) != TONE_NONE, "junk");
			int miss = 0;
			for (unsigned i = 0; i < TONE_DCS_COUNT; i++)
				if (TONE_DcsCode(TONE_FromDcsWord(TONE_Golay(TONE_DcsCodes[i]))) == 0)
					miss++;
			CHECK(!miss, "every code's word found: %d missed", miss);
		}

		gVfo.squelch = 3;
		gVfo.rx_tone = 885;
		at(14552000u);
		CHECK(css_mode == 1 && css_tenths == 885, "CTCSS set for receive");
		rssi = 200;
		css_heard_mask = 0;
		ticks(10);
		CHECK(RADIO_State() == RADIO_RX && !amp, "carrier without the tone: stays shut");
		css_heard_mask = 1u << BK4819_TONE_CTCSS;
		ticks(5);
		CHECK(RADIO_State() == RADIO_RX_OPEN && amp, "tone heard: opens");
		css_heard_mask = 0;
		ticks(10);
		CHECK(RADIO_State() == RADIO_RX_OPEN, "short drop-out: still open");
		ticks(15);
		CHECK(RADIO_State() == RADIO_RX && !amp, "tone gone: shuts");
		gVfo.rx_tone = 106;
		at(14552000u);
		CHECK(css_mode == 3 && css_word == 0x763813u, "DCS 023 inverted set");
		css_heard_mask = 1u << BK4819_TONE_DCS_N;
		ticks(10);
		CHECK(RADIO_State() == RADIO_RX, "normal DCS heard, inverted wanted: shut");
		css_heard_mask = 1u << BK4819_TONE_DCS_I;
		ticks(5);
		CHECK(RADIO_State() == RADIO_RX_OPEN, "inverted DCS heard: open");
		gVfo.rx_tone = 0;
		at(14552000u);
		CHECK(css_mode == 0, "no tone: sub-audio off");
		rssi = 60;
		css_heard_mask = 0;
		ticks(30);
	}

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

	/* scan timing: hop every dwell, the three resume modes */
	{
		int hops = 0, i;
		SCAN_Start(+1, SCAN_RESUME_CARRIER, 10);
		for (i = 0; i < 60; i++)
			hops += SCAN_Tick(false) == SCAN_HOP;
		CHECK(hops == 10, "hops in 600 ms: %d", hops);
		CHECK(SCAN_Tick(true) == SCAN_NONE && SCAN_Holding(), "stops on a signal");
		for (i = 0, hops = 0; i < 1000; i++)
			hops += SCAN_Tick(true) == SCAN_HOP;
		CHECK(!hops && SCAN_Holding(), "CARRIER: stays while the signal is there");
		for (i = 0; i < 199; i++)
			hops += SCAN_Tick(false) == SCAN_HOP;
		CHECK(!hops, "CARRIER: still holding at 1.99 s");
		CHECK(SCAN_Tick(false) == SCAN_HOP && !SCAN_Holding(), "CARRIER: moves on 2 s after it goes");

		SCAN_Start(-1, SCAN_RESUME_TIME, 10);
		SCAN_Tick(true);
		for (i = 0, hops = 0; i < 499; i++)
			hops += SCAN_Tick(true) == SCAN_HOP;
		CHECK(!hops && SCAN_Dir() == -1, "TIME: holding until 5 s");
		CHECK(SCAN_Tick(true) == SCAN_HOP, "TIME: moves on at 5 s with the signal still there");

		SCAN_Start(+1, SCAN_RESUME_SEARCH, 10);
		SCAN_Tick(false);
		CHECK(SCAN_Tick(true) == SCAN_IDLE && !SCAN_Active(), "SEARCH: ends on the first signal");
		SCAN_Start(+1, SCAN_RESUME_TIME, 10);
		SCAN_Stop();
		CHECK(SCAN_Tick(false) == SCAN_IDLE, "stopped");
	}

	/* dual watch timing */
	{
		int i, begins = 0, ends = 0;
		DwAction_t a;
		DW_Reset();
		for (i = 0; i < 29; i++)
			begins += DW_Tick(true, false, false, 10) == DW_BEGIN;
		CHECK(!begins, "active side for 290 ms");
		CHECK(DW_Tick(true, false, false, 10) == DW_BEGIN && DW_Peeking(), "peek at 300 ms");
		for (i = 0; i < 19; i++)
			ends += DW_Tick(true, false, false, 10) == DW_END;
		CHECK(!ends && DW_Tick(true, false, false, 10) == DW_END && !DW_Peeking(), "back after 200 ms");
		for (i = 0; i < 100; i++)
			begins += DW_Tick(true, false, true, 10) == DW_BEGIN;
		CHECK(!begins, "never peeks while the active side has a signal");
		for (i = 0; i < 30; i++)
			DW_Tick(true, false, false, 10);
		CHECK(DW_Peeking(), "peeking again");
		for (i = 0; i < 3; i++)
			a = DW_Tick(true, false, true, 10);
		CHECK(a == DW_NONE && !DW_Held(), "a signal in the first 40 ms after the retune is ignored (old side)");
		CHECK(DW_Tick(true, false, true, 10) == DW_HELD && DW_Held(), "the other side has a signal: held");
		for (i = 0; i < 300; i++)
			ends += DW_Tick(true, false, true, 10) == DW_END;
		for (i = 0; i < 99; i++)
			ends += DW_Tick(true, false, false, 10) == DW_END;
		CHECK(!ends && DW_Held(), "held while open and for 0.99 s after");
		CHECK(DW_Tick(true, false, false, 10) == DW_END, "back 1 s after it goes");
		for (i = 0; i < 30; i++)
			DW_Tick(true, false, false, 10);
		a = DW_Tick(true, true, false, 10);
		CHECK(a == DW_END && !DW_Peeking(), "busy (PTT, key): back at once");
		CHECK(DW_Tick(false, false, false, 10) == DW_NONE, "off");
	}

	/* DTMF on receive: only with the squelch open, mapped as the BK4819 codes */
	{
		gVfo.squelch = 3;
		gVfo.rx_tone = 0;
		at(14552000u);
		CHECK(dtmf_on, "DTMF decoder on in receive");
		rssi = 60;
		ticks(20);
		dtmf_code = 5;
		CHECK(RADIO_DtmfPoll() == 0, "no DTMF while shut");
		rssi = 200;
		ticks(5);
		dtmf_code = 14;
		CHECK(RADIO_DtmfPoll() == '*', "code 14 = *");
		dtmf_code = 15;
		CHECK(RADIO_DtmfPoll() == '#' && RADIO_DtmfPoll() == 0, "code 15 = #, then nothing");
		dtmf_code = 10;
		CHECK(RADIO_DtmfPoll() == 'A', "code 10 = A");
		rssi = 60;
		ticks(30);
	}

	/* VOX: the stock thresholds, 8 lower while transmitting, hold (delay + 5) x 100 ms */
	{
		int i, stops = 0;
		VOX_Reset();
		CHECK(VOX_Tick(41, 2, 6, false, 10) == VOX_NONE, "level 2: 41 is not above 41");
		CHECK(VOX_Tick(42, 2, 6, false, 10) == VOX_START, "level 2: 42 starts");
		CHECK(VOX_Tick(34, 2, 6, true, 10) == VOX_NONE, "while TX: 34 holds (41 - 8)");
		for (i = 0; i < 109; i++)
			stops += VOX_Tick(0, 2, 6, true, 10) == VOX_STOP;
		CHECK(!stops, "held 1.09 s after the last sound (delay 6: 1.1 s)");
		CHECK(VOX_Tick(0, 2, 6, true, 10) == VOX_STOP, "stops at 1.1 s");
		VOX_Reset();
		CHECK(VOX_Tick(111, 9, 0, false, 10) == VOX_NONE && VOX_Tick(112, 9, 0, false, 10) == VOX_START,
		      "level 9: threshold 111");
	}

	/* battery: the stock scale and the calibrated thresholds (made up here) */
	{
		CHECK(BATTERY_Level() == 0xFF, "no battery thresholds: no level");
		for (int i = 0; i < 6; i++)
			cal[0x200 + i] = (uint8_t)(140 + 13 * i);     /* 140 153 166 179 192 205 */
		CALIB_Set(cal);
		BATTERY_Set(183u << 2);
		CHECK(BATTERY_Raw() == 183 && BATTERY_Tenths() == 79, "183 reads 7.9 V: %u", BATTERY_Tenths());
		CHECK(BATTERY_Level() == 4 && !BATTERY_Low(), "level 4: %u", BATTERY_Level());
		for (int i = 0; i < 30; i++)
			BATTERY_Set(153u << 2);
		CHECK(BATTERY_Raw() == 153 && BATTERY_Level() == 1 && BATTERY_Low(), "at the 2nd threshold: low (stock >)");
		for (int i = 0; i < 30; i++)
			BATTERY_Set(140u << 2);
		CHECK(BATTERY_Level() == 1, "at the 1st threshold: passed (stock >=)");
		for (int i = 0; i < 20; i++)
			BATTERY_Set(100u << 2);
		CHECK(BATTERY_Level() == 0 && BATTERY_Raw() < 102, "flat: %u", BATTERY_Raw());
		BATTERY_Set(0xFFFF);
		CHECK(BATTERY_Raw() < 100, "no reading pulls down");
	}

	/* receive audio held off after transmit (repeater tail clear) */
	gVfo.squelch = 0;
	at(14552000u);
	RADIO_MuteRx(300);
	ticks(2);
	CHECK(!amp && af == BK4819_AF_MUTE, "muted while the count runs");
	ticks(30);
	CHECK(amp && af == BK4819_AF_FM, "audio back after 300 ms");

	/* tones sent while transmitting: the stock DTMF table, one tones of 1000-2100 Hz, roger beep */
	gVfo.power = POWER_LOW;
	at(14552000u);
	CHECK(!RADIO_TxTone(1, false), "no tone sent unless transmitting");
	if (RADIO_StartTx()) {
		CHECK(RADIO_TxTone(1, false) && tx_tones_on == 2 && tx_w1 == 0x1C1C && tx_w2 == 0x30C2 && !tx_mon,
		      "DTMF 1 = 697 + 1209 Hz, words as stock (%04X %04X)", tx_w1, tx_w2);
		CHECK(RADIO_TxTone(0, false) && tx_w1 == 0x25F3 && tx_w2 == 0x35E1, "DTMF 0 = 941 + 1336 Hz");
		CHECK(RADIO_TxTone(15, false) && tx_w1 == 0x25F3 && tx_w2 == 0x3B91, "DTMF # = 941 + 1477 Hz");
		CHECK(!RADIO_TxTone(16, false) && !RADIO_TxTone(21, false), "codes 16 and 21 are not sent");
		CHECK(RADIO_TxTone(19, true) && tx_tones_on == 1 && tx_w1 == 18067 && !tx_w2 && tx_mon &&
		      reg30 == 0xC3FA && amp, "tone 19 = 1750 Hz (stock 175 x 1032444 / 10000), heard locally, mic out");
		RADIO_TxToneOff();
		CHECK(!tx_tones_on && !tx_mon && reg30 == 0xC1FE && !RADIO_TxToneOn(), "tone off: back to the mic");
		RADIO_TxTone(1, true);
		tx_mon = 0;              /* would be set again if the monitor were switched for the next code */
		RADIO_TxToneSilence();
		CHECK(tx_tones_on == 2 && !tx_w1 && !tx_w2, "silence between codes leaves the generator on");
		CHECK(RADIO_TxTone(2, true) && tx_w1 == 0x1C1C && tx_w2 == 0x35E1 && !tx_mon, "next code: only the pair");
		RADIO_TxToneOff();
		tx_log_n = 0;
		delay_ms = 0;
		RADIO_Roger();
		CHECK(tx_log_n >= 2 && tx_log[0][0] == 0x2854 && !tx_log[0][1] && tx_log[1][0] == 0x2248 && delay_ms == 80 &&
		      !tx_tones_on && !tx_mon, "roger: 1000 Hz then 850 Hz, 40 ms each (stock words 0x2854, 0x2248)");
		gVfo.tx_tone = 0;
		RADIO_TxTail();
		CHECK(tail_mode == 0, "tail with no tone: 55 Hz as stock");
		gVfo.tx_tone = TONE_Ctcss[12];
		RADIO_TxTail();
		CHECK(tail_mode == 0, "tail with CTCSS: 55 Hz");
		gVfo.tx_tone = 5;
		RADIO_TxTail();
		CHECK(tail_mode == 1, "tail with DCS: turn-off code");
		gVfo.tx_tone = 0;
		RADIO_TxTone(5, true);
		RADIO_EndTx();
		tail_mode = -1;
		RADIO_TxTail();
		CHECK(tail_mode == -1, "no tail unless transmitting");
		CHECK(!tx_tones_on && !tx_mon && !RADIO_TxToneOn(), "transmit end stops a tone");
	} else
		CHECK(!RADIO_ENABLE_TX, "TX at 145.52 MHz");

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
				CHECK(mic_gain == CALIB_MicGain(), "mic gain from calibration at TX: %u", mic_gain);
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

	/* TX lock modes (radio/txlock.c): which frequencies each allows, and the local services' rules */
	{
		static const struct { TxLock_t m; Region_t r; uint32_t f; bool low, narrow; int ok; } L[] = {
			{ TXL_LOCKED,    REGION_US, 14552000u, true,  false, 0 },
			{ TXL_HAM,       REGION_US, 14700000u, true,  false, 1 },
			{ TXL_HAM,       REGION_EU, 14700000u, true,  false, 0 },   /* 2 m ends at 146 in Europe */
			{ TXL_HAM,       REGION_EU, 43500000u, true,  false, 1 },
			{ TXL_HAM,       REGION_EU, 44600000u, true,  false, 0 },
			{ TXL_HAM,       REGION_ITU, 14552000u, true, false, 1 },
			{ TXL_HAM,       REGION_ITU, 42500000u, true, false, 0 },
			{ TXL_LOCAL,     REGION_US, 46256250u, false, false, 1 },   /* GMRS 1 */
			{ TXL_LOCAL,     REGION_US, 46272500u, false, false, 1 },   /* GMRS 22 */
			{ TXL_LOCAL,     REGION_US, 46256000u, true,  true,  0 },   /* off channel */
			{ TXL_LOCAL,     REGION_US, 46756250u, false, true,  0 },   /* GMRS 8: high power */
			{ TXL_LOCAL,     REGION_US, 46756250u, true,  false, 0 },   /* GMRS 8: wide */
			{ TXL_LOCAL,     REGION_US, 46756250u, true,  true,  1 },
			{ TXL_LOCAL,     REGION_US, 46760000u, false, false, 1 },   /* repeater input */
			{ TXL_LOCAL,     REGION_US, 14552000u, true,  false, 0 },
			{ TXL_HAM_LOCAL, REGION_US, 14552000u, true,  false, 1 },
			{ TXL_HAM_LOCAL, REGION_US, 46255000u, false, false, 1 },
			{ TXL_HAM_LOCAL, REGION_US, 46000000u, false, false, 0 },
			{ TXL_LOCAL,     REGION_EU, 44600625u, true,  true,  1 },   /* PMR446 1 */
			{ TXL_LOCAL,     REGION_EU, 44619375u, true,  true,  1 },   /* PMR446 16 */
			{ TXL_LOCAL,     REGION_EU, 44620625u, true,  true,  0 },
			{ TXL_LOCAL,     REGION_EU, 44600625u, false, true,  0 },
			{ TXL_LOCAL,     REGION_AU, 47642500u, false, true,  1 },   /* UHF CB 1 */
			{ TXL_LOCAL,     REGION_AU, 47695000u, false, true,  0 },   /* 22: data */
			{ TXL_LOCAL,     REGION_AU, 47696250u, false, true,  0 },   /* 62: reserved */
			{ TXL_LOCAL,     REGION_AU, 47700000u, false, true,  1 },   /* 24 */
			{ TXL_LOCAL,     REGION_AU, 47741250u, false, true,  1 },   /* 80 */
			{ TXL_LOCAL,     REGION_AU, 47642500u, false, false, 0 },
			{ TXL_UNLOCKED,  REGION_US, 15600000u, false, false, 1 },
		};
		for (unsigned i = 0; i < sizeof L / sizeof L[0]; i++) {
			TXLOCK_Set(L[i].m, L[i].r);
			CHECK(TXLOCK_Mode() == L[i].m && TXLOCK_Region() == L[i].r, "lock %u set", i);
			const char *why = TXLOCK_Check(L[i].f, L[i].low, L[i].narrow);
			CHECK((why == 0) == (L[i].ok != 0), "lock %u (%s %s) at %lu: %s", i, TXLOCK_ModeName(L[i].m, L[i].r),
			      TXLOCK_RegionName(L[i].r), (unsigned long)L[i].f, why ? why : "ok");
		}
		TXLOCK_Set(TXL_LOCAL, REGION_ITU);
		CHECK(TXLOCK_Mode() == TXL_HAM, "no local service without a region: falls back to HAM");
		TXLOCK_Set(TXL_HAM, REGION_COUNT);
		CHECK(TXLOCK_Region() == REGION_DEFAULT, "bad region: default");

		/* through the radio: unlocked still needs a transmit path and calibration */
		TXLOCK_Set(TXL_UNLOCKED, REGION_US);
		gVfo.power = POWER_LOW;
		at(15600000u);
		CHECK(RADIO_StartTx() == RADIO_ENABLE_TX, "unlocked: TX at 156 MHz");
		RADIO_EndTx();
		at(22300000u);
		CHECK(!RADIO_StartTx(), "unlocked: no TX path at 223 MHz");
		at(12000000u);
		CHECK(!RADIO_StartTx(), "unlocked: no TX on airband");
		TXLOCK_Set(TXL_LOCKED, REGION_US);
		at(14552000u);
		CHECK(!RADIO_StartTx(), "locked: no TX at 145.52 MHz");
		TXLOCK_Set(TXL_HAM, REGION_US);
	}

	/* key beep: the receiver comes back when it ends, and PTT during a beep still transmits */
	{
		at(14552000u);
		RADIO_Beep(1000, 40);
		CHECK(reg30 == 0x0302, "beep: tone path on (%04X)", reg30);
		ticks(5);
		CHECK(reg30 == 0xBFF1, "beep over: receive back (%04X)", reg30);
		RADIO_Beep(1000, 400);
		CHECK(RADIO_StartTx() == RADIO_ENABLE_TX, "PTT during a beep");
		if (RADIO_ENABLE_TX) {
			ticks(50);
			CHECK(reg30 == 0xC1FE, "the beep's end leaves transmit alone (%04X)", reg30);
			RADIO_EndTx();
		}
	}

	/* PTT-ID sequence: wait, each code for the on time, silence for the off time, ends at 0xFF or 16 codes */
	{
		static const uint8_t up[16] = { 1, 2, 3, 0xFF };
		static const uint8_t full[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
		int seen[64], n = 0, c, t;
		CHECK(DTMFSEQ_TimeMs(0) == 50 && DTMFSEQ_TimeMs(1) == 100 && DTMFSEQ_TimeMs(5) == 500 &&
		      DTMFSEQ_TimeMs(9) == 100, "code times 50-500 ms, out of range = 100 ms");
		CHECK(DTMFSEQ_WaitMs(0) == 100 && DTMFSEQ_WaitMs(3) == 400, "wait 100 ms + 100 ms x setting 0x0C");
		CHECK(!DTMFSEQ_Start((const uint8_t[16]){ 0xFF }, 0, 100, 100) && !DTMFSEQ_Busy(), "empty code: nothing");
		CHECK(DTMFSEQ_Start(up, 200, 100, 50) && DTMFSEQ_Busy(), "up code starts");
		for (t = 10; t <= 2000 && n < 64; t += 10)
			if ((c = DTMFSEQ_Tick(10)) != DTMFSEQ_NONE)
				seen[n++] = c, seen[n++] = t;
		/* code 1 at 200 ms, gap at 300, code 2 at 350, gap 450, code 3 at 500, done at 600 */
		CHECK(n == 12 && seen[0] == 1 && seen[1] == 200 && seen[2] == DTMFSEQ_GAP && seen[3] == 300 &&
		      seen[4] == 2 && seen[5] == 350 && seen[8] == 3 && seen[9] == 500 && seen[10] == DTMFSEQ_DONE &&
		      seen[11] == 600 && !DTMFSEQ_Busy(), "up code timing (%d events)", n / 2);
		DTMFSEQ_Start(full, 0, 50, 50);
		for (n = 0, t = 0; t < 400 && DTMFSEQ_Busy(); t++)
			n += DTMFSEQ_Tick(10) >= 0;
		CHECK(n == 16 && !DTMFSEQ_Busy(), "16 codes at most (%d)", n);
		DTMFSEQ_Start(up, 0, 50, 50);
		DTMFSEQ_Stop();
		CHECK(!DTMFSEQ_Busy() && DTMFSEQ_Tick(10) == DTMFSEQ_NONE, "stop");
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
		CHECK(ch.tx_tone == 885 && ch.rx_tone == 0, "tones %u %u", ch.tx_tone, ch.rx_tone);
		CHECK(ch.ptt_id == 0, "PTT-ID off");
		memcpy(r2, rec, 32);
		r2[13] = 3;
		CHECK(CHANNEL_Decode(r2, &ch) && ch.ptt_id == 3, "PTT-ID from byte 13");
		memcpy(r2, rec, 32);
		r2[8] = 0x6A;            /* index 106 = D023I */
		r2[10] = 0x01;           /* index 1 = D023N */
		r2[11] = 0x00;
		CHECK(CHANNEL_Decode(r2, &ch) && ch.rx_tone == 106 && ch.tx_tone == 1, "DCS tones %u %u", ch.rx_tone, ch.tx_tone);
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
