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
