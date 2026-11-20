/* Open-UV5R-Mini: key-test application.
 *
 * Shows the scan code of the key being pressed, both PTT inputs, the BK4819's REG_00, the RSSI and the
 * Bluetooth state, so the unknown keys and pins can be identified on a real radio. It never transmits.
 * Bytes received over Bluetooth are echoed back (a loopback test for the Bluetooth edition).
 */
#include <stdbool.h>
#include <stdint.h>
#include "app/app.h"
#include "app/update.h"
#include "board.h"
#include "bt/bt.h"
#include "driver/bk4819.h"
#include "driver/gpio.h"
#include "driver/keyboard.h"
#include "driver/st7735s.h"
#include "ui/helper.h"

#define WHITE  RGB565(255, 255, 255)
#define BLACK  RGB565(0, 0, 0)
#define YELLOW RGB565(255, 220, 0)
#define GREEN  RGB565(0, 220, 80)
#define RED    RGB565(255, 40, 40)
#define GREY   RGB565(120, 120, 120)

#define TEST_FREQ_10HZ 14550000u /* 145.500 MHz, receive only */

static KEY_Code_t last_key = KEY_INVALID;
static uint8_t off_count;
static uint16_t bt_rx_count;

static void on_bt_receive(const uint8_t *data, uint16_t len)
{
	bt_rx_count += len;
	BT_Send(data, len);
}

static void draw_static(void)
{
	UI_PrintString("OPEN-UV5R-MINI", 22, 2, YELLOW, BLACK, 1);
	UI_PrintString("KEY TEST", 40, 12, WHITE, BLACK, 1);
	UI_PrintString("KEY", 4, 28, WHITE, BLACK, 2);
	UI_PrintString("PTT A", 4, 56, WHITE, BLACK, 1);
	UI_PrintString("PTT B", 4, 66, WHITE, BLACK, 1);
	UI_PrintString("BK REG00", 4, 80, WHITE, BLACK, 1);
	UI_PrintString("RSSI", 4, 90, WHITE, BLACK, 1);
	UI_PrintString("BT", 4, 104, WHITE, BLACK, 1);
	UI_PrintString("RX 145.500", 4, 118, GREY, BLACK, 1);
}

void APP_Init(void)
{
	char buf[8];

	BOARD_Init();
	ST7735S_Init();
	KEYBOARD_Init();
	if (UPDATE_Requested())
		UPDATE_Mode();          /* PTT-A + '=' at power-on, as the stock app */
	BK4819_Init();
	BK4819_SetFrequency(TEST_FREQ_10HZ);
	BK4819_SetMode(BK4819_MODE_RX);
	BT_Init(on_bt_receive);

	draw_static();
	UI_PrintString(UI_FormatHex(buf, BK4819_ReadRegister(BK4819_REG_00), 4), 64, 80, GREEN, BLACK, 1);
}

void APP_Tick(void)
{
	char buf[8];
	KEY_Code_t key = KEYBOARD_Poll();

	if (key != last_key) {
		if (key == KEY_INVALID)
			UI_PrintString("--", 52, 28, GREEN, BLACK, 3);
		else
			UI_PrintString(UI_FormatDec(buf, key, 2), 52, 28, GREEN, BLACK, 3);
		last_key = key;
	}

	bool ptt = KEYBOARD_IsPttPressed(), ptt2 = KEYBOARD_IsPtt2Pressed();
	UI_PrintString(ptt ? "DOWN" : "UP  ", 64, 56, ptt ? RED : GREEN, BLACK, 1);
	UI_PrintString(ptt2 ? "DOWN" : "UP  ", 64, 66, ptt2 ? RED : GREEN, BLACK, 1);
	UI_PrintString(UI_FormatDec(buf, BK4819_GetRSSI(), 3), 64, 90, GREEN, BLACK, 1);

	if (!BT_IsAvailable())
		UI_PrintString("NOT BUILT", 28, 104, GREY, BLACK, 1);
	else if (BT_IsConnected()) {
		UI_PrintString("RX ", 28, 104, GREEN, BLACK, 1);
		UI_PrintString(UI_FormatDec(buf, bt_rx_count, 5), 46, 104, GREEN, BLACK, 1);
	} else
		UI_PrintString("WAITING  ", 28, 104, YELLOW, BLACK, 1);

	/* power switch: about 30 ms debounce, like the stock firmware's 25 ms */
	if (!GPIO_CheckBit(PIN_POWER_SENSE)) {
		if (++off_count >= 3)
			BOARD_PowerOff();
	} else {
		off_count = 0;
	}
}
