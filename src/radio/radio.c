/* Open-UV5R-Mini: the radio core. See radio/radio.h. */
#include "radio/ax25.h"
#include "radio/radio.h"
#include "store/calib.h"
#include "radio/tones.h"
#include "radio/txlock.h"
#include "board.h"
#include "driver/bk4819.h"
#include "driver/gpio.h"
#include "driver/io.h"
#include "driver/system.h"

Vfo_t gVfo;

const uint16_t RADIO_Steps[] = { 250, 500, 625, 1000, 1250, 2000, 2500, 5000, 10000 };
const uint8_t  RADIO_StepCount = sizeof RADIO_Steps / sizeof RADIO_Steps[0];

static RadioState_t s_state;
static uint16_t s_rssi;
static uint8_t s_open_count, s_close_count;
static uint16_t s_beep_ms;
static uint8_t s_tone_count, s_tone_miss;     /* debounce of the receive tone */
static bool s_tone_heard;
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

/* ------------------------------------------------------------------------------------------- sub-audio */
static void css_set(uint16_t tone)
{
	switch (TONE_Kind(tone)) {
	case TONE_CTCSS:
		BK4819_SetCtcss(tone, CALIB_CtcssLevel());
		break;
	case TONE_DCS_N:
	case TONE_DCS_I:
		BK4819_SetDcs(TONE_Golay(TONE_DcsCode(tone)), TONE_Kind(tone) == TONE_DCS_I, CALIB_DcsLevel());
		break;
	default:
		BK4819_CssOff();
		break;
	}
}

static BK4819_Tone_t css_kind(uint16_t tone)
{
	switch (TONE_Kind(tone)) {
	case TONE_CTCSS: return BK4819_TONE_CTCSS;
	case TONE_DCS_N: return BK4819_TONE_DCS_N;
	case TONE_DCS_I: return BK4819_TONE_DCS_I;
	default:         return BK4819_TONE_OFF;
	}
}

bool RADIO_ToneHeard(void) { return s_tone_heard; }

/* ------------------------------------------------------------------------------------------- receive */
static uint16_t s_mute_ms;     /* receive audio held off after transmit (repeater tail) */
static bool     s_monitor;     /* squelch held open (RADIO_Monitor) */
static bool     s_parked;      /* RADIO_Park: the BK4819 asleep while the MCU's FM receiver has the VFO */
static bool     s_rx_asleep;   /* the BK4819 is asleep (RADIO_RxSleep) until RADIO_Apply or transmit */
static void beep_end(void);
static bool     s_probe;       /* the spectrum has the receiver (RADIO_ProbeBegin) */

static void audio(bool open)
{
	if (s_mute_ms || s_parked)
		open = false;
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
	if (s_state == RADIO_TX || s_parked)   /* parked: gVfo is kept, the chip stays asleep until RADIO_Park(false) */
		return;
	s_rx_asleep = false;               /* BK4819_RxOn below wakes the chip (REG_37) */
	if (s_beep_ms) {                   /* retuning puts receive back on, which would leave the beep half set up */
		s_beep_ms = 0;
		BK4819_Beep(0);
		IO_Amp(AUDIO_BEEP, false);
	}
	BK4819_SetFrequency(gVfo.freq);
	BK4819_SetFilterBandwidth(gVfo.narrow || RADIO_IsAm());
	BK4819_SetVolume(gVfo.volume);
	front_end(band_of(gVfo.freq), false);
	BK4819_SetPa(0, 0);
	gVfo.rx_tone = TONE_Normalise(gVfo.rx_tone);
	gVfo.tx_tone = TONE_Normalise(gVfo.tx_tone);
	css_set(gVfo.rx_tone);
	s_tone_count = s_tone_miss = 0;
	s_tone_heard = gVfo.rx_tone == 0;
	BK4819_RxOn();
	BK4819_DtmfRxOn();
	squelch_levels();
	s_state = RADIO_RX;
	s_open_count = s_close_count = 0;
	audio(gVfo.squelch == 0 || s_monitor);
	if (gVfo.squelch == 0 || s_monitor)
		s_state = RADIO_RX_OPEN;
}

/* Monitor (K5 feature 1.13): the squelch held open, as squelch 0, until turned off; then the squelch decides again. */
void RADIO_Monitor(bool on)
{
	if (s_monitor == on)
		return;
	s_monitor = on;
	if (s_state == RADIO_TX || s_parked)
		return;
	if (on) {
		s_state = RADIO_RX_OPEN;
		if (!s_beep_ms)
			audio(true);
	} else if (gVfo.squelch) {
		s_state = RADIO_RX;
		s_open_count = 0;
		if (!s_beep_ms)
			audio(false);
	}
}

bool RADIO_MonitorOn(void) { return s_monitor; }

/* Parked (broadcast FM in the VFO, app.c): the BK4819 asleep with its audio off, as RADIO_RxSleep, and kept so:
 * RADIO_Apply only clamps gVfo, the squelch and monitor don't open, no beeps, no transmit (RADIO_TxRefusal).
 * RADIO_Park(false) wakes it on gVfo again. */
void RADIO_Park(bool on)
{
	if (on == s_parked || (on && s_state == RADIO_TX))
		return;
	if (on) {
		RADIO_ProbeListen(0);            /* only acts while the spectrum is parked listening */
		RADIO_RxSleep();
		s_parked = true;
	} else {
		s_parked = false;
		RADIO_Apply();
	}
}

bool RADIO_Parked(void) { return s_parked; }

/* The receiver asleep, for the radio's sleep mode: audio off, then the BK4819 off as stock's power saving and
 * F4HWN's sleep do (REG_30 = 0, REG_37 = 0x1D00: BK4819_Sleep). Nothing is heard until RADIO_Apply wakes it. */
void RADIO_RxSleep(void)
{
	if (s_state == RADIO_TX || s_rx_asleep)
		return;
	if (s_beep_ms)
		beep_end();
	audio(false);
	s_state = RADIO_RX;
	s_open_count = s_close_count = 0;
	BK4819_Sleep();
	s_rx_asleep = true;
}

bool RADIO_RxAsleep(void) { return s_rx_asleep; }

/* Probe, for the spectrum app: the audio muted and the squelch left alone while the receiver is moved from
 * frequency to frequency and its RSSI read. As F4HWN's spectrum (its app/spectrum.c SetF and GetRssi): set the
 * frequency, REG_30 off and on again so the VCO re-calibrates, wait (its default 3.2 ms), wait while REG_63's low
 * byte (a glitch level) is 200 or more, then read REG_67 twice and keep the second (the AGC may still be moving).
 * On a radio (2026-11-24) our first version (1.5 ms, one read) read about 0 everywhere. RADIO_ProbeEnd tunes back
 * to gVfo. */
void RADIO_ProbeBegin(void)
{
	if (s_state == RADIO_TX)
		return;
	if (s_beep_ms)
		beep_end();
	s_monitor = false;
	audio(false);
	s_state = RADIO_RX;
	s_open_count = s_close_count = 0;
	s_probe = true;
}

static Band_t s_probe_band = (Band_t)0xFF;   /* the front end ProbeRssi last set */
static bool   s_listen;                      /* RADIO_ProbeListen: parked with the audio on */

uint16_t RADIO_ProbeRssi(uint32_t f, uint16_t settle_us)
{
	Band_t b = band_of(f);

	if (!s_probe)
		return 0;
	if (s_listen)
		RADIO_ProbeListen(0);
	if (b != s_probe_band) {
		front_end(b, false);
		s_probe_band = b;
	}
	BK4819_SetFrequency(f);
	BK4819_RxOn();
	s_rx_asleep = false;
	SYSTEM_DelayUs(settle_us);
	for (unsigned guard = 50; guard && (BK4819_ReadRegister(0x63) & 0xFFu) >= 200u; guard--)
		SYSTEM_DelayUs(1);
	(void)BK4819_GetRSSI();
	return BK4819_GetRSSI();
}

/* Parked on f to hear it (Sean, 2026-11-24: a station tuned on the spectrum was never heard, as the sweep keeps the
 * audio off). Wide filter, sub-audio off, the audio path opened as RADIO_Monitor does; AM in the airband when the
 * VFO's modulation is AUTO. Broadcast FM (75 kHz deviation) is wider than the BK4819's widest filter, so it is heard
 * loud and distorted; the AC695N's own FM receiver (ROADMAP 3.9) is what plays it properly. */
void RADIO_ProbeListen(uint32_t f)
{
	bool am;

	if (!s_probe)
		return;
	if (f == 0) {
		if (s_listen) {
			s_listen = false;
			BK4819_SetAF(BK4819_AF_MUTE);
			IO_Amp(AUDIO_RX, false);
			IO_LedRx(false);
		}
		return;
	}
	am = gVfo.mod == MOD_AM || (gVfo.mod == MOD_AUTO && band_of(f) == BAND_AIR);
	s_probe_band = band_of(f);
	front_end(s_probe_band, false);
	BK4819_SetFrequency(f);
	BK4819_SetFilterBandwidth(am);
	BK4819_SetVolume(gVfo.volume);
	BK4819_CssOff();
	BK4819_RxOn();
	s_rx_asleep = false;
	s_listen = true;
	BK4819_SetAF(am ? BK4819_AF_AM : BK4819_AF_FM);
	IO_Amp(AUDIO_RX, true);
	IO_LedRx(true);
}

uint16_t RADIO_ProbeLevel(void)
{
	return s_listen ? BK4819_GetRSSI() : 0;
}

void RADIO_ProbeEnd(void)
{
	if (!s_probe)
		return;
	RADIO_ProbeListen(0);
	s_probe = false;
	s_probe_band = (Band_t)0xFF;
	RADIO_Apply();
}

void RADIO_Init(void)
{
	gVfo.freq = 14552000u;     /* 145.520 MHz */
	gVfo.step = 4;             /* 12.5 kHz */
	gVfo.squelch = 3;
	gVfo.volume = 10;
	gVfo.narrow = false;
	gVfo.mod = MOD_AUTO;
	gVfo.rx_tone = 0;
	gVfo.tx_tone = 0;
	gVfo.power = POWER_LOW;
	gVfo.tx_freq = 0;
	gVfo.rx_only = false;
	IO_Init();
	GPIO_ConfigOutput(PIN_RX_FRONT, true);
	s_state = RADIO_RX;
	RADIO_Apply();
}

/* Squelch the stock way: the thresholds go into the BK4819 (REG_78, open << 8 | close) and the chip's own squelch
 * result, REG_0C bit 1, opens and closes the audio (stock sub_01E4DE3C, read by the squelch-open routine
 * sub_01E4E408). Thresholds: the radio's own (store/calib.h), else level 1 opens at about -122 dBm, each level 5 dB
 * more, 3 dB hysteresis. An RSSI compare in software (the first receiver build) opened only for a blink on a radio
 * (2026-11-21), so the RSSI is now read only for the bar. */
static void squelch_levels(void)
{
	uint8_t o, c;

	if (CALIB_Present())
		CALIB_Squelch(gVfo.freq, gVfo.squelch, &o, &c);
	else {
		o = (uint8_t)((-127 + 5 * gVfo.squelch + 160) * 2);
		c = (uint8_t)(o - 6u);
	}
	BK4819_WriteRegister(BK4819_REG_78, (uint16_t)(o << 8 | c));
}

/* A signal is there, tone or not: the chip's own squelch result (REG_0C bit 1, no tone in it). Dual watch uses it
 * so a carrier keeps it on a side while the tone detector locks, which takes a couple of hundred ms (radio,
 * 2026-11-22: with dual watch on, the other side never received). Not the RSSI against the REG_78 threshold: our
 * software RSSI compare was already wrong on a radio (squelch_levels), so it may have held dual watch on the active
 * side for ever. With squelch 0 the audio is always open, so only the chip's squelch counts. */
bool RADIO_Carrier(void)
{
	if (s_state == RADIO_TX || s_rx_asleep || s_probe)
		return false;
	return BK4819_SquelchOpen() || (gVfo.squelch && !s_monitor && s_state == RADIO_RX_OPEN);
}

/* The beep's tone off and receive back on (BK4819_Beep turns the chip's receive path off while it plays). */
static void beep_end(void)
{
	s_beep_ms = 0;
	BK4819_Beep(0);
	IO_Amp(AUDIO_BEEP, false);
	if (s_state != RADIO_TX) {
		BK4819_RxOn();
		audio(s_state == RADIO_RX_OPEN);
	}
}

void RADIO_Tick(void)
{
	if (s_mute_ms && (s_mute_ms = s_mute_ms > 10u ? (uint16_t)(s_mute_ms - 10u) : 0) == 0 && s_state == RADIO_RX_OPEN)
		audio(true);
	if (s_beep_ms) {
		if (s_beep_ms > 10u)
			s_beep_ms -= 10u;
		else
			beep_end();
	}
	if (s_state == RADIO_TX || s_beep_ms || s_rx_asleep || s_probe)   /* off while a beep plays, asleep, probing */
		return;
	s_rssi = BK4819_GetRSSI();
	if (gVfo.squelch == 0 || s_monitor) {
		if (s_state != RADIO_RX_OPEN) {
			s_state = RADIO_RX_OPEN;
			audio(true);
		}
		return;
	}
	/* Tone squelch: heard for 30 ms to open, missing for 200 ms to close (DCS and CTCSS decoders drop out briefly
	 * on a weak signal). */
	if (gVfo.rx_tone) {
		if (BK4819_CssHeard(css_kind(gVfo.rx_tone))) {
			s_tone_miss = 0;
			if (s_tone_count < 3u && ++s_tone_count == 3u)
				s_tone_heard = true;
		} else {
			s_tone_count = 0;
			if (s_tone_miss < 20u && ++s_tone_miss == 20u)
				s_tone_heard = false;
		}
	}
	bool sq = BK4819_SquelchOpen();
	if (s_state == RADIO_RX) {
		s_open_count = sq ? s_open_count + 1 : 0;
		if (s_open_count >= 3 && s_tone_heard) {
			s_state = RADIO_RX_OPEN;
			s_close_count = 0;
			if (!s_beep_ms)
				audio(true);
		}
	} else {
		s_close_count = !sq ? s_close_count + 1 : 0;
		if (s_close_count >= 15 || !s_tone_heard) {         /* 150 ms of quiet, or the tone gone */
			s_state = RADIO_RX;
			s_open_count = 0;
			if (!s_beep_ms)
				audio(false);
		}
	}
}

RadioState_t RADIO_State(void) { return s_state; }

/* A DTMF character heard on receive, or 0 (only while the squelch is open, so noise can't make codes). */
char RADIO_DtmfPoll(void)
{
	static const char MAP[16] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', '*', '#' };
	int c;

	if (s_state != RADIO_RX_OPEN)
		return 0;
	c = BK4819_DtmfTake();
	return c < 0 ? 0 : MAP[c & 15];
}
uint16_t RADIO_Rssi(void) { return s_rssi; }

/* ------------------------------------------------------------------------------------------- transmit
 * Only where the TX lock allows (radio/txlock.h; the amateur bands by default, AGENTS.md rule 5), and only through
 * a transmit path the hardware has. */
static uint32_t tx_freq(void)
{
	return gVfo.tx_freq ? gVfo.tx_freq : gVfo.freq;
}

const char *RADIO_TxRefusal(void)
{
	uint32_t f = tx_freq();
	Band_t b = band_of(f);
	const char *why;

	if (!RADIO_ENABLE_TX)
		return "TX OFF IN BUILD";
	if (s_parked)
		return "NO TX ON BROADCAST";
	if (gVfo.rx_only)
		return "RX ONLY CHANNEL";
	if (RADIO_IsAm())
		return "NO TX IN AM";
	if ((why = TXLOCK_Check(f, gVfo.power == POWER_LOW, gVfo.narrow)) != 0)
		return why;
	if (b == BAND_AIR || b == BAND_220)
		return "NO TX PATH HERE";
	if (!CALIB_Present())
		return "NO TX: NO CALIB";
	if (CALIB_PaBias(f, gVfo.power) == 0)       /* no calibration for this frequency: no TX */
		return "NO TX: CALIB 0";
	return 0;
}

bool RADIO_TxAllowed(void) { return RADIO_TxRefusal() == 0; }

bool RADIO_StartTx(void)
{
	if (s_state == RADIO_TX || !RADIO_TxAllowed())
		return false;
	if (s_beep_ms)
		beep_end();
	audio(false);
	BK4819_DtmfRxOff();
	s_rx_asleep = false;
	s_probe = s_listen = false;    /* the TX set-up writes REG_37 again (BK4819_TxOn) */
	s_probe_band = (Band_t)0xFF;
	s_state = RADIO_TX;
	BK4819_SetFrequency(tx_freq());
	BK4819_SetFilterBandwidth(gVfo.narrow);
	front_end(band_of(tx_freq()), true);
	css_set(gVfo.tx_tone);
	BK4819_SetMicGain(CALIB_MicGain());
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
	RADIO_TxToneOff();
	BK4819_SetPa(0, 0);
	BK4819_CssOff();
	front_end(band_of(gVfo.freq), false);
	IO_LedTx(false);
	s_state = RADIO_RX;
	RADIO_Apply();
}

/* ------------------------------------------------------------------------------------------- tones over the air */
/* DTMF pairs in the order of the stock table (0x01E66136): 0-9, A, B, C, D, *, #. */
static const uint16_t DTMF_HZ[16][2] = {
	{ 941, 1336 }, { 697, 1209 }, { 697, 1336 }, { 697, 1477 }, { 770, 1209 }, { 770, 1336 }, { 770, 1477 },
	{ 852, 1209 }, { 852, 1336 }, { 852, 1477 }, { 697, 1633 }, { 770, 1633 }, { 852, 1633 }, { 941, 1633 },
	{ 941, 1209 }, { 941, 1477 },
};
/* codes 17-20, stock bytes in 10 Hz at 0x01E654A2 */
static const uint16_t SINGLE_HZ[4] = { 1000, 1450, 1750, 2100 };
static bool s_txtone, s_txmon, s_txdual;

bool RADIO_TxTone(uint8_t code, bool monitor)
{
	if (s_state != RADIO_TX || (code > 15 && (code < 17 || code > 20)))
		return false;
	if (s_txtone && s_txdual && code < 16 && s_txmon == monitor) {     /* the next code of a sequence */
		BK4819_TxTonePair(BK4819_ToneWord(DTMF_HZ[code][0]), BK4819_ToneWord(DTMF_HZ[code][1]));
		return true;
	}
	RADIO_TxToneOff();
	BK4819_TxTonesOn(code < 16);
	if (monitor) {
		IO_Amp(AUDIO_TONE, true);
		BK4819_TxMonitor(true);
	}
	if (code < 16)
		BK4819_TxTonePair(BK4819_ToneWord(DTMF_HZ[code][0]), BK4819_ToneWord(DTMF_HZ[code][1]));
	else
		BK4819_TxTonePair((uint16_t)((uint32_t)(SINGLE_HZ[code - 17] / 10u) * 1032444u / 10000u), 0);   /* stock sub_01E40B52 */
	s_txtone = true;
	s_txdual = code < 16;
	s_txmon = monitor;
	return true;
}

/* An AX.25 frame as Bell 202 AFSK (APRS), while transmitting: the one-tone generator in the stock DTMF-sidetone
 * mode (mic off, heard locally), its frequency word changed at each bit, 1200 Hz or 2200 Hz, 1200 bits a second
 * timed on Timer 2. The generator keeps its phase when the word changes (to be checked on a radio). Blocks for
 * the whole frame: about 1 s for a position report with 300 ms of flags first. */
bool RADIO_SendAfsk(const uint8_t *frame, unsigned len, unsigned pre_flags)
{
	Ax25Tx_t t;
	int lv;
	uint32_t t0, tps = SYSTEM_TicksPerSec(), k = 0;
	const uint16_t mark = BK4819_ToneWord(1200), space = BK4819_ToneWord(2200);

	if (!RADIO_TxTone(17, true))
		return false;
	AX25_TxStart(&t, frame, len, pre_flags, 3);
	t0 = SYSTEM_Raw();
	while ((lv = AX25_TxNext(&t)) >= 0) {
		BK4819_TxTonePair(lv ? mark : space, 0);
		k++;
		while (SYSTEM_Raw() - t0 < k * (tps / 8u) / 150u)      /* k / 1200 s, no 64-bit division */
			;
		if (!(k & 63u))
			SYSTEM_WatchdogClear();
	}
	RADIO_TxToneOff();
	return true;
}

void RADIO_TxToneSilence(void)
{
	if (s_txtone)
		BK4819_TxTonePair(0, 0);
}

void RADIO_TxToneOff(void)
{
	if (!s_txtone)
		return;
	BK4819_TxTonesOff();
	if (s_txmon) {
		BK4819_TxMonitor(false);
		BK4819_SetVolume(gVfo.volume);
		IO_Amp(AUDIO_TONE, false);
	}
	s_txtone = s_txmon = false;
}

bool RADIO_TxToneOn(void) { return s_txtone; }

void RADIO_Roger(void)
{
	if (s_state != RADIO_TX)
		return;
	RADIO_TxToneOff();
	BK4819_TxTonesOn(true);
	IO_Amp(AUDIO_TONE, true);
	BK4819_TxMonitor(true);
	BK4819_TxTonePair(BK4819_ToneWord(1000), 0);
	SYSTEM_DelayMs(40);
	BK4819_TxTonePair(BK4819_ToneWord(850), 0);
	SYSTEM_DelayMs(40);
	s_txtone = s_txmon = true;
	RADIO_TxToneOff();
}

void RADIO_MuteRx(uint16_t ms)
{
	s_mute_ms = ms;
	if (ms)
		audio(false);
}

void RADIO_TxTail(void)
{
	ToneKind_t k = TONE_Kind(gVfo.tx_tone);

	if (s_state != RADIO_TX)
		return;
	BK4819_TailTone(k == TONE_DCS_N || k == TONE_DCS_I, CALIB_CtcssLevel());
}

/* A short local tone (key beep, errors), never sent over the air. */
bool (*gRadioBeepHook)(uint16_t hz, uint16_t ms);

void RADIO_Beep(uint16_t hz, uint16_t ms)
{
	/* not while transmitting; not while the spectrum probes either: each step turns the receive path on again,
	 * which with the beep's audio routing played receiver noise (radio, 2026-11-24: "white noise" on UP/DOWN) */
	if (s_state == RADIO_TX)
		return;
	if (gRadioBeepHook && gRadioBeepHook(hz, ms))
		return;                          /* the MCU's DAC played it (app.c): the BK4819 untouched */
	if (s_probe || s_parked)
		return;
	IO_Amp(AUDIO_BEEP, true);
	BK4819_Beep(hz);
	s_beep_ms = ms;
}
