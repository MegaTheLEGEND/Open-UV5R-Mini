/* Open-UV5R-Mini: keypad scan, same algorithm as the stock KEYBOARD_Poll (sub_01E46C8C).
 *
 * Rows PA6..PA9 are driven low one at a time; columns PC2..PC5 have pull-ups. Step 0 drives no row,
 * which finds the four keys wired straight to GND.
 */
#include "driver/keyboard.h"
#include "board.h"
#include "driver/gpio.h"
#include "driver/system.h"

#define ROW_MASK 0x3C0u /* PA6..PA9 */
#define COL_MASK 0x03Cu /* PC2..PC5 */

static const uint16_t ROW_SET[5]   = { 0x3C0, 0x380, 0x340, 0x2C0, 0x1C0 };
static const uint16_t ROW_CLEAR[5] = { 0x000, 0x040, 0x080, 0x100, 0x200 };

static const uint8_t KEY_TABLE[5][4] = { /* [step][column PC2..PC5] */
	{  0,  3,  4,  2 },
	{  8,  7,  6,  5 },
	{ 20, 19, 18, 17 },
	{ 16, 15, 14, 13 },
	{ 12, 11, 10,  9 },
};

void KEYBOARD_Init(void)
{
	GPIO_ConfigOutput(PIN_KEY_ROW0, false);
	GPIO_ConfigOutput(PIN_KEY_ROW1, false);
	GPIO_ConfigOutput(PIN_KEY_ROW2, false);
	GPIO_ConfigOutput(PIN_KEY_ROW3, false);
	GPIO_ConfigInput(PIN_KEY_COL0, true);
	GPIO_ConfigInput(PIN_KEY_COL1, true);
	GPIO_ConfigInput(PIN_KEY_COL2, true);
	GPIO_ConfigInput(PIN_KEY_COL3, true);
	GPIO_ConfigInput(PIN_PTT, true);
	GPIO_ConfigInput(PIN_PTT2, true);
}

static int column_low(uint32_t cols)
{
	switch (cols & COL_MASK) {
	case 0x38: return 0; /* PC2 low */
	case 0x34: return 1; /* PC3 low */
	case 0x2C: return 2; /* PC4 low */
	case 0x1C: return 3; /* PC5 low */
	default:   return -1;
	}
}

KEY_Code_t KEYBOARD_Poll(void)
{
	KEY_Code_t key = KEY_INVALID;

	GPIOA->OUT &= ~ROW_MASK; /* all rows low: is anything pressed at all? */
	SYSTEM_DelayUs(1);
	if ((GPIOC->IN & COL_MASK) == COL_MASK)
		goto done;

	for (unsigned step = 0; step < 5; step++) {
		GPIOA->OUT |= ROW_SET[step];
		GPIOA->OUT &= ~ROW_CLEAR[step];
		SYSTEM_DelayUs(1);
		int col = column_low(GPIOC->IN);
		if (col >= 0) {
			key = (KEY_Code_t)KEY_TABLE[step][col];
			break;
		}
	}
done:
	GPIOA->OUT |= ROW_MASK;
	return key;
}

bool KEYBOARD_IsPttPressed(void)  { return !GPIO_CheckBit(PIN_PTT); }
bool KEYBOARD_IsPtt2Pressed(void) { return !GPIO_CheckBit(PIN_PTT2); }
