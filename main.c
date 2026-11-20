/* Open-UV5R-Mini: first bring-up image.
 *
 * Shows the scan code of the key being pressed, both PTT inputs, the BK4819's REG_00 and the RSSI, so the
 * unknown keys and pins can be identified on a real radio. It never transmits.
 */
#include <stdbool.h>
#include <stdint.h>
#include "board.h"
#include "driver/bk4819.h"
#include "driver/gpio.h"
#include "driver/keyboard.h"
#include "driver/st7735s.h"
#include "driver/system.h"
#include "ui/helper.h"

#define WHITE  RGB565(255, 255, 255)
#define BLACK  RGB565(0, 0, 0)
#define YELLOW RGB565(255, 220, 0)
#define GREEN  RGB565(0, 220, 80)
#define RED    RGB565(255, 40, 40)

#define TEST_FREQ_10HZ 14550000u /* 145.500 MHz, receive only */

static void draw_static(void)
{
	UI_PrintString("OPEN-UV5R-MINI", 22, 2, YELLOW, BLACK, 1);
	UI_PrintString("KEY TEST", 40, 12, WHITE, BLACK, 1);
	UI_PrintString("KEY", 4, 30, WHITE, BLACK, 2);
	UI_PrintString("PTT A", 4, 62, WHITE, BLACK, 1);
	UI_PrintString("PTT B", 4, 74, WHITE, BLACK, 1);
	UI_PrintString("BK REG00", 4, 92, WHITE, BLACK, 1);
	UI_PrintString("RSSI", 4, 104, WHITE, BLACK, 1);
	UI_PrintString("RX 145.500", 4, 116, WHITE, BLACK, 1);
}

int main(void)
{
	char buf[8];
	KEY_Code_t last_key = KEY_INVALID;
	uint8_t off_count = 0;

	BOARD_Init();
	ST7735S_Init();
	KEYBOARD_Init();
	BK4819_Init();
	BK4819_SetFrequency(TEST_FREQ_10HZ);
	BK4819_SetMode(BK4819_MODE_RX);

	draw_static();
	UI_PrintString(UI_FormatHex(buf, BK4819_ReadRegister(BK4819_REG_00), 4), 64, 92, GREEN, BLACK, 1);

	for (;;) {
		KEY_Code_t key = KEYBOARD_Poll();
		if (key != last_key) {
			if (key == KEY_INVALID)
				UI_PrintString("--", 52, 30, GREEN, BLACK, 3);
			else
				UI_PrintString(UI_FormatDec(buf, key, 2), 52, 30, GREEN, BLACK, 3);
			last_key = key;
		}

		UI_PrintString(KEYBOARD_IsPttPressed() ? "DOWN" : "UP  ", 64, 62, KEYBOARD_IsPttPressed() ? RED : GREEN, BLACK, 1);
		UI_PrintString(KEYBOARD_IsPtt2Pressed() ? "DOWN" : "UP  ", 64, 74, KEYBOARD_IsPtt2Pressed() ? RED : GREEN, BLACK, 1);
		UI_PrintString(UI_FormatDec(buf, BK4819_GetRSSI(), 3), 64, 104, GREEN, BLACK, 1);

		/* power switch: 25 ms debounce like the stock firmware */
		if (!GPIO_CheckBit(PIN_POWER_SENSE)) {
			if (++off_count >= 3)
				BOARD_PowerOff();
		} else {
			off_count = 0;
		}
		SYSTEM_DelayMs(10);
	}
}
