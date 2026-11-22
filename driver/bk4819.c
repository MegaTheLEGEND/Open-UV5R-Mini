/* Open-UV5R-Mini: BK4819 RF transceiver driver.
 *
 * Bus timing, init values and mode values are the ones the stock UV-5R Mini firmware uses
 * (sub_01E409C4 read, sub_01E3E0B2 write, sub_01E46F3E init, sub_01E3FC64 mode switch).
 */
#include "driver/bk4819.h"
#include "board.h"
#include "driver/gpio.h"
#include "driver/system.h"

#define BUS_DELAY() SYSTEM_DelayUs(1) /* stock: sub_01E3E042(5) */

static void BK4819_WriteU8(uint8_t data)
{
	for (uint8_t bit = 0x80; bit; bit >>= 1) {
		GPIO_ClearBit(PIN_BK4819_SCK);
		GPIO_Write(PIN_BK4819_SDATA, data & bit);
		BUS_DELAY();
		GPIO_SetBit(PIN_BK4819_SCK);
		BUS_DELAY();
	}
}

uint16_t BK4819_ReadRegister(uint8_t reg)
{
	uint16_t value = 0;

	GPIO_ClearBit(PIN_BK4819_SCN);
	BUS_DELAY();
	BK4819_WriteU8(reg | 0x80);           /* bit 7 = read */
	GPIO_ClearBit(PIN_BK4819_SCK);
	BUS_DELAY();
	GPIO_SetInput(PIN_BK4819_SDATA);
	BUS_DELAY();
	for (uint16_t bit = 0x8000; bit; bit >>= 1) {
		GPIO_SetBit(PIN_BK4819_SCK);
		value <<= 1;
		BUS_DELAY();
		if (GPIO_CheckBit(PIN_BK4819_SDATA))
			value |= 1;
		GPIO_ClearBit(PIN_BK4819_SCK);
		BUS_DELAY();
	}
	GPIO_SetOutput(PIN_BK4819_SDATA);
	GPIO_SetBit(PIN_BK4819_SCN);
	BUS_DELAY();
	GPIO_ClearBit(PIN_BK4819_SCK);
	GPIO_ClearBit(PIN_BK4819_SDATA);
	return value;
}

void BK4819_WriteRegister(uint8_t reg, uint16_t value)
{
	GPIO_ClearBit(PIN_BK4819_SCN);
	BUS_DELAY();
	BK4819_WriteU8(reg);
	BK4819_WriteU8(value >> 8);
	BK4819_WriteU8(value & 0xFF);
	BUS_DELAY();
	GPIO_SetBit(PIN_BK4819_SCN);
	BUS_DELAY();
	GPIO_ClearBit(PIN_BK4819_SCK);
	GPIO_ClearBit(PIN_BK4819_SDATA);
}

/* Init sequence in the order the stock firmware writes it (sub_01E46F3E, 0x01E46FFA..0x01E471C8). */
static const uint16_t AGC_TABLE[16] = {
	0x006F, 0x106B, 0x2067, 0x3062, 0x4050, 0x5047, 0x603A, 0x702C,
	0x8041, 0x9037, 0xA025, 0xB017, 0xC0E4, 0xD0CB, 0xE0B5, 0xF09F,
};

void BK4819_Init(void)
{
	GPIO_ConfigOutput(PIN_BK4819_SCN, true);
	GPIO_ConfigOutput(PIN_BK4819_SCK, false);
	GPIO_ConfigOutput(PIN_BK4819_SDATA, false);

	BK4819_WriteRegister(BK4819_REG_00, 0x8000);
	BK4819_WriteRegister(BK4819_REG_00, 0x0000);
	BK4819_WriteRegister(BK4819_REG_37, 0x1D0F);
	BK4819_WriteRegister(BK4819_REG_13, 0x03BE);
	BK4819_WriteRegister(BK4819_REG_12, 0x037B);
	BK4819_WriteRegister(BK4819_REG_11, 0x027B);
	BK4819_WriteRegister(BK4819_REG_10, 0x007A);
	BK4819_WriteRegister(BK4819_REG_14, 0x0019);
	BK4819_WriteRegister(BK4819_REG_49, 0x2A38);
	BK4819_WriteRegister(BK4819_REG_7B, 0x8420);
	BK4819_WriteRegister(BK4819_REG_19, 0x1041);
	BK4819_WriteRegister(BK4819_REG_2A, 0x4F18);
	for (unsigned i = 0; i < 16; i++)
		BK4819_WriteRegister(BK4819_REG_09, AGC_TABLE[i]);
	BK4819_WriteRegister(BK4819_REG_21, 0x06D8);
	BK4819_WriteRegister(BK4819_REG_72, 0x3065);
	BK4819_WriteRegister(BK4819_REG_5C, 0x5665);
	BK4819_WriteRegister(BK4819_REG_5D, 0x0F00);
	/* stock reads REG_40 here and writes back 0x04D2 into it (deviation); kept as a plain write */
	BK4819_WriteRegister(BK4819_REG_40, 0x04D2);
	BK4819_WriteRegister(BK4819_REG_75, 0xD42D);
	BK4819_WriteRegister(BK4819_REG_54, 0x91C1);
	BK4819_WriteRegister(BK4819_REG_55, 0x3040);
	BK4819_WriteRegister(BK4819_REG_28, 0x6B38);
	BK4819_WriteRegister(BK4819_REG_7D, 0xE952);
	BK4819_WriteRegister(BK4819_REG_48, 0xB3BF);
	BK4819_WriteRegister(BK4819_REG_1F, 0x5454);
	BK4819_WriteRegister(BK4819_REG_3E, 0xA037);
	BK4819_WriteRegister(BK4819_REG_77, 0x88EF);
	BK4819_WriteRegister(BK4819_REG_4F, 0x3732);
	BK4819_WriteRegister(BK4819_REG_26, 0x13A0);
}

void BK4819_SetMode(BK4819_Mode_t mode)
{
	static const uint16_t REG30[] = { 0x0000, 0xBFF1, 0xC1FE, 0x0302, 0xC3FA };

	BK4819_WriteRegister(BK4819_REG_30, 0x0000);
	if (mode != BK4819_MODE_OFF && mode < sizeof(REG30) / sizeof(REG30[0]))
		BK4819_WriteRegister(BK4819_REG_30, REG30[mode]);
}

void BK4819_SetFrequency(uint32_t freq_10hz)
{
	BK4819_WriteRegister(BK4819_REG_38, freq_10hz & 0xFFFF);
	BK4819_WriteRegister(BK4819_REG_39, freq_10hz >> 16);
}

uint16_t BK4819_GetRSSI(void)
{
	return BK4819_ReadRegister(BK4819_REG_67) & 0x01FF;
}

/* Same register order as the stock sub_01E4ECB4: REG_69, REG_6A, then REG_68. */
BK4819_CssScanResult_t BK4819_GetCxCSSScanResult(uint32_t *cdcss, uint16_t *ctcss)
{
	uint16_t high = BK4819_ReadRegister(BK4819_REG_69);

	if ((high & 0x8000) == 0) {
		uint16_t low = BK4819_ReadRegister(BK4819_REG_6A);
		*cdcss = ((uint32_t)(high & 0xFFF) << 12) | (low & 0xFFF);
		return BK4819_CSS_CDCSS;
	}

	uint16_t raw = BK4819_ReadRegister(BK4819_REG_68);
	if ((raw & 0x8000) == 0) {
		*ctcss = (uint16_t)(((uint32_t)(raw & 0x1FFF) * 4843) / 10000); /* to 0.1 Hz units */
		return BK4819_CSS_CTCSS;
	}
	return BK4819_CSS_NONE;
}

/* ------------------------------------------------------------------------------------------------------------------
 * Radio functions. Register facts from the stock firmware where it has them (addresses given), else from the BK4819
 * notes of the UV-K5 open firmware (DualTachyon, Apache-2.0), used as a reference only; this code is written fresh.
 */

/* REG_47 = 0x6040 | type << 8; the stock app uses 0x6040 | 0x600 for AM on this radio (sub_01E40AA0, table at
 * 0x01E5DF08: 0x000 mute, 0x100 FM, 0x200, 0x300 beep, 0x600 AM). */
void BK4819_SetAF(BK4819_AF_t af)
{
	BK4819_WriteRegister(BK4819_REG_47, (uint16_t)(0x6040u | ((uint16_t)af << 8)));
}

/* AF DAC gain, REG_48 bits 4..9 (stock: (vol << 4 | 0x800F) & 0x83FF; 0xB3BF at init). level 0..15. */
void BK4819_SetVolume(uint8_t level)
{
	uint16_t g = level > 15 ? 63 : (uint16_t)(level * 4u + (level ? 3u : 0u));

	BK4819_WriteRegister(BK4819_REG_48, (uint16_t)(0xB00Fu | (g << 4)) & 0xB3FFu);
}

void BK4819_SetFilterBandwidth(bool narrow)
{
	BK4819_WriteRegister(BK4819_REG_43, narrow ? 0x4048 : 0x3028);
}

/* BK4819 GPIO pins through REG_33 as the stock app drives them (sub_01E4135C): clear mask << 8 to make the pin an
 * output, then set or clear mask in the low byte. */
void BK4819_SetGpio(uint8_t mask, bool on)
{
	uint16_t r = BK4819_ReadRegister(BK4819_REG_33);

	r &= (uint16_t)~((uint16_t)mask << 8);
	if (on)
		r |= mask;
	else
		r &= (uint16_t)~mask;
	BK4819_WriteRegister(BK4819_REG_33, r);
}

/* Squelch thresholds: RSSI (REG_78), noise (REG_4F), glitch (REG_4D / REG_4E). */
void BK4819_SetupSquelch(uint8_t open_rssi, uint8_t close_rssi, uint8_t open_noise, uint8_t close_noise,
                         uint8_t open_glitch, uint8_t close_glitch)
{
	BK4819_WriteRegister(BK4819_REG_70, 0);
	BK4819_WriteRegister(BK4819_REG_4D, (uint16_t)(0xA000u | close_glitch));
	BK4819_WriteRegister(BK4819_REG_4E, (uint16_t)(0x6F00u | open_glitch));
	BK4819_WriteRegister(BK4819_REG_4F, (uint16_t)(close_noise << 8 | open_noise));
	BK4819_WriteRegister(BK4819_REG_78, (uint16_t)(open_rssi << 8 | close_rssi));
}

/* Interrupt flags (REG_02 bits, enabled in REG_3F): squelch found 3, lost 2; CTCSS found 9, lost 8 (reference). */
void BK4819_SetInterrupts(uint16_t mask)
{
	BK4819_WriteRegister(BK4819_REG_3F, mask);
}

/* Returns the flags of a pending interrupt (REG_0C bit 0), and clears it; 0 if none. */
uint16_t BK4819_TakeInterrupts(void)
{
	uint16_t f;

	if (!(BK4819_ReadRegister(BK4819_REG_0C) & 1u))
		return 0;
	BK4819_WriteRegister(BK4819_REG_02, 0);
	f = BK4819_ReadRegister(BK4819_REG_02);
	return f ? f : 0x8000u;
}

/* Receive on: REG_37 0x1D0F (stock sub_01E40D34), REG_30 off then 0xBFF1 (stock mode 1). */
void BK4819_RxOn(void)
{
	BK4819_WriteRegister(BK4819_REG_37, 0x1D0F);
	BK4819_WriteRegister(BK4819_REG_30, 0x0000);
	BK4819_WriteRegister(BK4819_REG_30, 0xBFF1);
}

/* Everything off, as the stock TX/RX switch does for power saving: REG_30 = 0, REG_37 = 0x1D00. */
void BK4819_Sleep(void)
{
	BK4819_WriteRegister(BK4819_REG_30, 0x0000);
	BK4819_WriteRegister(BK4819_REG_37, 0x1D00);
}

/* Sub-audio, CTCSS. The same setting serves transmit (the tone is sent) and receive (REG_0C bit 10 says the tone
 * is heard), as stock sub_01E4D97A: REG_51 = (level | 0x9000) & 0x907F, REG_07 = tenths * 206489 / 100000 (CTC1),
 * REG_07 = 0x21CD (CTC2, the squelch-tail tone), REG_52 = 0x0292. level is the radio's own (calibration 0xF211). */
void BK4819_SetCtcss(uint16_t tenths, uint8_t level)
{
	BK4819_WriteRegister(BK4819_REG_51, (uint16_t)((level | 0x9000u) & 0x907Fu));
	BK4819_WriteRegister(BK4819_REG_07, (uint16_t)(((uint32_t)tenths * 206489u) / 100000u));
	BK4819_WriteRegister(BK4819_REG_07, 0x21CD);
	BK4819_WriteRegister(BK4819_REG_52, 0x0292);
}

/* Sub-audio, DCS, as stock sub_01E4D880: REG_51 = 0x8000 (normal) or 0xA000 (inverted) | level (calibration
 * 0xF212, bit 7 cleared), REG_07 = 2775, then the 23-bit word in two halves through REG_08. Receive: REG_0C bit 14
 * (normal) or 15 (inverted) says the code is heard. */
void BK4819_SetDcs(uint32_t word, bool inverted, uint8_t level)
{
	BK4819_WriteRegister(BK4819_REG_51, (uint16_t)((inverted ? 0xA000u : 0x8000u) | (level & 0x7Fu)));
	BK4819_WriteRegister(BK4819_REG_07, 2775);
	BK4819_WriteRegister(BK4819_REG_08, (uint16_t)(word & 0xFFFu));
	BK4819_WriteRegister(BK4819_REG_08, (uint16_t)(0x8000u | (word >> 12 & 0xFFFu)));
}

/* Squelch tail elimination when transmit ends, as stock sub_01E4D7BA(1): with no tone or CTCSS, a 55 Hz tone
 * (REG_51 = (level | 0x9000) & 0x907F, REG_07 = 1137 = 55.06 Hz) and REG_52 bits 13-15 cleared; with DCS, REG_52 bit
 * 15 set (bits 13-14 cleared). */
void BK4819_TailTone(bool dcs, uint8_t level)
{
	uint16_t r52 = BK4819_ReadRegister(BK4819_REG_52) & 0x1FFFu;

	if (dcs)
		r52 |= 0x8000u;
	else {
		BK4819_WriteRegister(BK4819_REG_51, (uint16_t)((level | 0x9000u) & 0x907Fu));
		BK4819_WriteRegister(BK4819_REG_07, 1137);
	}
	BK4819_WriteRegister(BK4819_REG_52, r52);
}

/* Sub-audio scan (stock: mode off, REG_51 = 0x0300, mode RX; the 0x01E4FCE6 path of the tone scan). */
void BK4819_CssScanStart(void)
{
	BK4819_WriteRegister(BK4819_REG_30, 0x0000);
	BK4819_WriteRegister(BK4819_REG_51, 0x0300);
	BK4819_WriteRegister(BK4819_REG_30, 0xBFF1);
}

/* What the scan heard, as stock sub_01E4ECB4 reads it: REG_69 bit 15 clear = a DCS word (REG_69 low 12 bits above
 * REG_6A low 12 bits); else REG_68 bit 15 clear = a CTCSS frequency, returned here in 0.1 Hz (stock: raw * 100000 /
 * 206489, the inverse of BK4819_SetCtcss). */
BK4819_Tone_t BK4819_CssScanRead(uint32_t *value)
{
	uint16_t hi = BK4819_ReadRegister(BK4819_REG_69), r;

	if (!(hi & 0x8000u)) {
		*value = (uint32_t)(hi & 0xFFFu) << 12 | (BK4819_ReadRegister(BK4819_REG_6A) & 0xFFFu);
		return BK4819_TONE_DCS_N;
	}
	r = BK4819_ReadRegister(BK4819_REG_68);
	if (!(r & 0x8000u)) {
		*value = (uint32_t)(r & 0x1FFFu) * 100000u / 206489u;
		return BK4819_TONE_CTCSS;
	}
	return BK4819_TONE_OFF;
}

/* DTMF decoder on receive, as stock sub_01E4D40A: REG_24 = 0x8A7F, REG_3F = 0x0800 (only the "DTMF found"
 * interrupt). Off as stock sub_01E4111C: REG_24 bit 5 cleared. */
void BK4819_DtmfRxOn(void)
{
	BK4819_WriteRegister(BK4819_REG_24, 0x8A7F);
	BK4819_WriteRegister(BK4819_REG_3F, 0x0800);
}

void BK4819_DtmfRxOff(void)
{
	BK4819_WriteRegister(BK4819_REG_24, (uint16_t)(BK4819_ReadRegister(BK4819_REG_24) & ~0x20u));
	BK4819_WriteRegister(BK4819_REG_3F, 0x0000);
}

/* Tones sent while transmitting, the stock way.
 * - DTMF generator on (stock sub_01E4D40A(1)): REG_24 = 0x8A7F, REG_70 = 0xE0E0 (both tone generators, gain 0x60),
 *   REG_3F = 0x0800. A pair goes in REG_71 / REG_72 (sub_01E4D44C). Off as the DTMF decoder (REG_24 bit 5 cleared,
 *   sub_01E4111C), and REG_70 = 0.
 * - One tone (sub_01E40B24(1)): REG_70 = (REG_70 | 0xBC00) & 0xFCFF; REG_71 from sub_01E40B52.
 * - Frequency word: Hz x 10.32444 rounded, as stock's table at 0x01E66136 (941 Hz = 0x25F3, 770 Hz = 0x1F0E).
 * - Local monitor (stock mode 4 and SetAF(241), sub_01E3FC64 / sub_01E40AA0): REG_30 = 0 then 0xC3FA (the mic is
 *   left out), REG_47 = 0x6140 (AF out) and REG_48 = 0xB8A2 (a fixed AF gain). Back: REG_30 = 0xC1FE.
 */
uint16_t BK4819_ToneWord(uint16_t hz)
{
	return (uint16_t)(((uint32_t)hz * 1032444u + 50000u) / 100000u);
}

void BK4819_TxTonesOn(bool dual)
{
	if (dual) {
		BK4819_WriteRegister(BK4819_REG_24, 0x8A7F);
		BK4819_WriteRegister(BK4819_REG_70, 0xE0E0);
		BK4819_WriteRegister(BK4819_REG_3F, 0x0800);
	} else
		BK4819_WriteRegister(BK4819_REG_70,
		                     (uint16_t)((BK4819_ReadRegister(BK4819_REG_70) | 0xBC00u) & 0xFCFFu));
}

void BK4819_TxTonePair(uint16_t w1, uint16_t w2)
{
	BK4819_WriteRegister(BK4819_REG_71, w1);
	BK4819_WriteRegister(BK4819_REG_72, w2);
}

void BK4819_TxTonesOff(void)
{
	BK4819_TxTonePair(0, 0);
	BK4819_WriteRegister(BK4819_REG_24, (uint16_t)(BK4819_ReadRegister(BK4819_REG_24) & ~0x20u));
	BK4819_WriteRegister(BK4819_REG_70, 0);
}

void BK4819_TxMonitor(bool on)
{
	BK4819_WriteRegister(BK4819_REG_30, 0x0000);
	BK4819_WriteRegister(BK4819_REG_30, on ? 0xC3FA : 0xC1FE);
	if (on) {
		BK4819_SetAF(BK4819_AF_FM);
		BK4819_WriteRegister(BK4819_REG_48, 0xB8A2);
	} else
		BK4819_SetAF(BK4819_AF_MUTE);
}

/* A DTMF code heard since the last call (0-15: 0-9, A, B, C, D, *, #), or -1. Stock: REG_02 cleared, then the code
 * in REG_0B bits 8-11 (0x01E53CC0). */
int BK4819_DtmfTake(void)
{
	uint16_t f = BK4819_TakeInterrupts();

	if (!(f & 0x0800u))
		return -1;
	return (BK4819_ReadRegister(BK4819_REG_0B) >> 8) & 0xF;
}

void BK4819_CssOff(void)
{
	BK4819_WriteRegister(BK4819_REG_51, 0x0000);
}

/* Which sub-audio is heard now, from REG_0C as stock sub_01E4DE5E reads it. */
bool BK4819_CssHeard(BK4819_Tone_t kind)
{
	uint16_t r = BK4819_ReadRegister(BK4819_REG_0C);

	switch (kind) {
	case BK4819_TONE_CTCSS:  return (r & 0x0400u) != 0;
	case BK4819_TONE_DCS_N:  return (r & 0x4000u) != 0;
	case BK4819_TONE_DCS_I:  return (r & 0x8000u) != 0;
	default:                return true;
	}
}

/* PA: REG_36 = bias << 8 | gain byte (stock sub_01E4DA78: the gain byte comes from a table by power level, 0xFF or
 * 0xD7; bias 0 writes 0x007F = PA off). */
void BK4819_SetPa(uint8_t bias, uint8_t gain)
{
	BK4819_WriteRegister(BK4819_REG_36, bias ? (uint16_t)(bias << 8 | gain) : 0x007F);
}

/* Mic gain: REG_7D low 5 bits, as stock sets it when transmit starts (0x01E504FE: (REG_7D & ~0x1F) | (calibration
 * 0xF210 & 0x1F)). */
void BK4819_SetMicGain(uint8_t gain)
{
	BK4819_WriteRegister(BK4819_REG_7D, (uint16_t)((BK4819_ReadRegister(BK4819_REG_7D) & ~0x1Fu) | (gain & 0x1Fu)));
}

void BK4819_TxMute(bool on)
{
	BK4819_WriteRegister(BK4819_REG_50, on ? 0xBB20 : 0x3B20);
}

/* Transmit on: REG_37 0x1D0F, REG_52, REG_30 0 then 0xC1FE (stock mode 2). */
void BK4819_TxOn(void)
{
	BK4819_WriteRegister(BK4819_REG_37, 0x1D0F);
	BK4819_WriteRegister(BK4819_REG_52, 0x028F);
	BK4819_WriteRegister(BK4819_REG_30, 0x0000);
	BK4819_WriteRegister(BK4819_REG_30, 0xC1FE);
}

/* A local tone through the speaker (key beep): tone 1 on, AF = beep. hz 0 = off. */
void BK4819_Beep(uint16_t hz)
{
	if (!hz) {
		BK4819_WriteRegister(BK4819_REG_70, 0);
		return;
	}
	BK4819_WriteRegister(BK4819_REG_70, (uint16_t)(0x8000u | 96u << 8));
	BK4819_WriteRegister(BK4819_REG_71, (uint16_t)(((uint32_t)hz * 1032444u) / 100000u));
	BK4819_SetAF(BK4819_AF_BEEP);
}

/* The chip's own squelch result (RSSI, noise and glitch against REG_78 / REG_4F / REG_4D-4E): REG_0C bit 1, set
 * while open. Stock sub_01E4DE3C reads it this way. */
bool BK4819_SquelchOpen(void)
{
	return (BK4819_ReadRegister(BK4819_REG_0C) & 0x0002u) != 0;
}

uint8_t BK4819_GetNoise(void)
{
	return (uint8_t)(BK4819_ReadRegister(BK4819_REG_65) & 0x7F);
}
