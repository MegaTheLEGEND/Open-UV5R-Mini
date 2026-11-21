/* PC test of app/radio.c with stand-in drivers (tests/radio_host.py builds it twice: transmit off and on).
 * Checks the transmit guard (amateur bands only, never airband or AM, never with RADIO_ENABLE_TX 0), the band switch
 * outputs, and the squelch opening and closing on the RSSI. */
#include <stdio.h>
#include <string.h>
#include "app/radio.h"
#include "board.h"
#include "driver/bk4819.h"
#include "driver/io.h"

bool test_pins[128];
static uint16_t rssi = 100, reg33, reg30, reg36;
static int amp, led_rx, led_tx;
static BK4819_AF_t af;

uint16_t BK4819_GetRSSI(void) { return rssi; }
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

	/* transmit guard */
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
				CHECK(reg30 == 0xC1FE && led_tx && (reg36 >> 8) && !amp, "TX on at %lu", (unsigned long)T[i].f);
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
	}

	printf(fails ? "radio_host (TX %d): %d FAILED\n" : "radio_host (TX %d): all passed\n", RADIO_ENABLE_TX, fails);
	return fails != 0;
}
