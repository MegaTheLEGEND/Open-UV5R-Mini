/* Open-UV5R-Mini: the radio core. See app/radio.h. */
#include "app/radio.h"
#include "app/calib.h"
#include "board.h"
#include "driver/bk4819.h"
#include "driver/gpio.h"
#include "driver/io.h"
#include "driver/system.h"

Vfo_t gVfo;

const uint16_t RADIO_Steps[] = { 250, 500, 625, 1000, 1250, 2000, 2500, 5000, 10000 };
const uint8_t  RADIO_StepCount = sizeof RADIO_Steps / sizeof RADIO_Steps[0];

/* The standard 50 CTCSS tones, 0.1 Hz units. */
const uint16_t RADIO_Ctcss[] = {
	670, 693, 719, 744, 770, 797, 825, 854, 885, 915, 948, 974, 1000, 1035, 1072, 1109, 1148,
	1188, 1230, 1273, 1318, 1365, 1413, 1462, 1500, 1567, 1598, 1622, 1655, 1679, 1713, 1738,
	1773, 1799, 1835, 1862, 1899, 1928, 1966, 1995, 2035, 2065, 2107, 2181, 2257, 2291, 2336,
	2418, 2503, 2541,
};
const uint8_t RADIO_CtcssCount = sizeof RADIO_Ctcss / sizeof RADIO_Ctcss[0];

static RadioState_t s_state;
static uint16_t s_rssi;
static uint8_t s_open_count, s_close_count;
static uint16_t s_beep_ms;
static void squelch_levels(void);

/* ------------------------------------------------------------------------------------------- bands
 * The stock app sorts a frequency into five bands (sub_01E3F682 / sub_01E3F6AA) and switches the front end by band
 * (sub_01E41382): below 300 MHz PC7 high and BK4819 GPIO5 off, 300 MHz and up PC7 low and GPIO5 on; transmit
 * through GPIO3 (VHF) or GPIO4 (UHF); 108-136 MHz and 200-300 MHz have no transmit path. */
typedef enum { BAND_VHF = 0, BAND_UHF = 1, BAND_220 = 2, BAND_350 = 3, BAND_AIR = 4 } Band_t;

static Band_t band_of(uint32_t f)
{
	if (f >= 10800000u && f < 13600000u)
		return BAND_AIR;
	if (f < 20000000u)
		return BAND_VHF;
	if (f < 30000000u)
		return BAND_220;
	if (f < 40000000u)
		return BAND_350;
	return BAND_UHF;
}

static void front_end(Band_t band, bool tx)
{
	bool uhf_rx = band == BAND_UHF || band == BAND_350;

	if (tx) {
		BK4819_SetGpio(BK4819_GPIO_TX_VHF, band == BAND_VHF);
		BK4819_SetGpio(BK4819_GPIO_TX_UHF, band == BAND_UHF || band == BAND_350);
	} else {
		BK4819_SetGpio(BK4819_GPIO_TX_VHF, false);
		BK4819_SetGpio(BK4819_GPIO_TX_UHF, false);
		GPIO_Write(PIN_RX_FRONT, !uhf_rx);
		BK4819_SetGpio(BK4819_GPIO_RX_UHF, uhf_rx);
	}
}

bool RADIO_IsAm(void)
{
	return gVfo.mod == MOD_AM || (gVfo.mod == MOD_AUTO && band_of(gVfo.freq) == BAND_AIR);
}

/* ------------------------------------------------------------------------------------------- receive */
static void audio(bool open)
{
	BK4819_SetAF(open ? (RADIO_IsAm() ? BK4819_AF_AM : BK4819_AF_FM) : BK4819_AF_MUTE);
	IO_Amp(AUDIO_RX, open);
	IO_LedRx(open);
}

void RADIO_Apply(void)
{
	if (gVfo.freq < FREQ_MIN)
		gVfo.freq = FREQ_MIN;
	if (gVfo.freq > FREQ_MAX)
		gVfo.freq = FREQ_MAX;
	if (s_state == RADIO_TX)
		return;
	BK4819_SetFrequency(gVfo.freq);
	BK4819_SetFilterBandwidth(gVfo.narrow || RADIO_IsAm());
	BK4819_SetVolume(gVfo.volume);
	front_end(band_of(gVfo.freq), false);
	BK4819_SetPa(0, 0);
	BK4819_SetTxCtcss(0);
	BK4819_RxOn();
	squelch_levels();
	s_state = RADIO_RX;
	s_open_count = s_close_count = 0;
	audio(gVfo.squelch == 0);
	if (gVfo.squelch == 0)
		s_state = RADIO_RX_OPEN;
}

void RADIO_Init(void)
{
	gVfo.freq = 14552000u;     /* 145.520 MHz */
	gVfo.step = 4;             /* 12.5 kHz */
	gVfo.squelch = 3;
	gVfo.volume = 10;
	gVfo.narrow = false;
	gVfo.mod = MOD_AUTO;
	gVfo.ctcss_tx = 0;
	gVfo.power = POWER_LOW;
	gVfo.tx_freq = 0;
	gVfo.rx_only = false;
	IO_Init();
	GPIO_ConfigOutput(PIN_RX_FRONT, true);
	s_state = RADIO_RX;
	RADIO_Apply();
}

/* Squelch on the RSSI (REG_67, 0.5 dB steps: dBm = raw / 2 - 160), a few ticks of debounce each way.
 * Thresholds: the radio's own (app/calib.h; the stock app writes them to REG_78, which compares them with the
 * RSSI, so they are taken to be in the same units), else level 1 opens at about -122 dBm, each level 5 dB more,
 * 3 dB hysteresis. */
static uint16_t s_open_at, s_close_at;

static void squelch_levels(void)
{
	if (CALIB_Present()) {
		uint8_t o, c;
		CALIB_Squelch(gVfo.freq, gVfo.squelch, &o, &c);
		s_open_at = o;
		s_close_at = c;
		BK4819_WriteRegister(BK4819_REG_78, (uint16_t)(o << 8 | c));
	} else {
		s_open_at = (uint16_t)((-127 + 5 * gVfo.squelch + 160) * 2);
		s_close_at = s_open_at - 6u;
	}
}

void RADIO_Tick(void)
{
	if (s_beep_ms) {
		if (s_beep_ms > 10u)
			s_beep_ms -= 10u;
		else {
			s_beep_ms = 0;
			BK4819_Beep(0);
			IO_Amp(AUDIO_BEEP, false);
			if (s_state != RADIO_TX)
				audio(s_state == RADIO_RX_OPEN);
		}
	}
	if (s_state == RADIO_TX)
		return;
	s_rssi = BK4819_GetRSSI();
	if (gVfo.squelch == 0) {
		if (s_state != RADIO_RX_OPEN) {
			s_state = RADIO_RX_OPEN;
			audio(true);
		}
		return;
	}
	if (s_state == RADIO_RX) {
		s_open_count = s_rssi >= s_open_at ? s_open_count + 1 : 0;
		if (s_open_count >= 3) {
			s_state = RADIO_RX_OPEN;
			s_close_count = 0;
			if (!s_beep_ms)
				audio(true);
		}
	} else {
		s_close_count = s_rssi < s_close_at ? s_close_count + 1 : 0;
		if (s_close_count >= 15) {         /* 150 ms of quiet */
			s_state = RADIO_RX;
			s_open_count = 0;
			if (!s_beep_ms)
				audio(false);
		}
	}
}

RadioState_t RADIO_State(void) { return s_state; }
uint16_t RADIO_Rssi(void) { return s_rssi; }

/* ------------------------------------------------------------------------------------------- transmit
 * Only inside these amateur bands (AGENTS.md rule 5), and only through a transmit path the hardware has. */
static const struct { uint32_t lo, hi; } TX_BANDS[] = {
	{ 14400000u, 14800000u },   /* 2 m */
	{ 42000000u, 45000000u },   /* 70 cm */
};

static uint32_t tx_freq(void)
{
	return gVfo.tx_freq ? gVfo.tx_freq : gVfo.freq;
}

bool RADIO_TxAllowed(void)
{
	uint32_t f = tx_freq();
	Band_t b = band_of(f);

	if (!RADIO_ENABLE_TX || gVfo.rx_only || RADIO_IsAm() || b == BAND_AIR || b == BAND_220)
		return false;
	if (CALIB_PaBias(f, gVfo.power) == 0)       /* no calibration for this frequency: no TX */
		return false;
	for (unsigned i = 0; i < sizeof TX_BANDS / sizeof TX_BANDS[0]; i++)
		if (f >= TX_BANDS[i].lo && f < TX_BANDS[i].hi)
			return true;
	return false;
}

bool RADIO_StartTx(void)
{
	if (s_state == RADIO_TX || !RADIO_TxAllowed())
		return false;
	audio(false);
	s_state = RADIO_TX;
	BK4819_SetFrequency(tx_freq());
	BK4819_SetFilterBandwidth(gVfo.narrow);
	front_end(band_of(tx_freq()), true);
	BK4819_SetTxCtcss(gVfo.ctcss_tx);
	BK4819_TxMute(false);
	BK4819_TxOn();
	BK4819_SetPa(CALIB_PaBias(tx_freq(), gVfo.power), CALIB_PaGain(gVfo.power));
	IO_LedTx(true);
	return true;
}

void RADIO_EndTx(void)
{
	if (s_state != RADIO_TX)
		return;
	BK4819_SetPa(0, 0);
	BK4819_SetTxCtcss(0);
	front_end(band_of(gVfo.freq), false);
	IO_LedTx(false);
	s_state = RADIO_RX;
	RADIO_Apply();
}

/* A short local tone (key beep, errors), never sent over the air. */
void RADIO_Beep(uint16_t hz, uint16_t ms)
{
	if (s_state == RADIO_TX)
		return;
	IO_Amp(AUDIO_BEEP, true);
	BK4819_Beep(hz);
	s_beep_ms = ms;
}
