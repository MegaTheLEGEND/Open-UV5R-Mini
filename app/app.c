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
#include "driver/system.h"
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
static uint32_t ticks; /* uptime: if it keeps dropping back to 0, something (the watchdog?) resets the chip */
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
	UI_PrintString(BT_IsAvailable() ? "ON" : "--", 28, 104, GREY, BLACK, 1);
}

void APP_Tick(void)
{
	char buf[8];
	KEY_Code_t key = KEYBOARD_Poll();

	static uint8_t last_ptt = 0xFF, last_ptt2 = 0xFF, last_sense = 0xFF;
	static uint16_t last_rssi = 0xFFFF;

	SYSTEM_WatchdogClear();

	/* heartbeat: counts up every 10 ticks; if it keeps going back to 0, the chip is restarting */
	if (++ticks % 10 == 0)
		UI_PrintString(UI_FormatDec(buf, ticks / 10 % 10000, 4), 100, 12, GREY, BLACK, 1);

	/* Drawing is slow (bit-banged), so only redraw what changed. */
	if (key != last_key) {
		if (key == KEY_INVALID)
			UI_PrintString("--", 52, 28, GREEN, BLACK, 3);
		else
			UI_PrintString(UI_FormatDec(buf, key, 2), 52, 28, GREEN, BLACK, 3);
		last_key = key;
	}

	uint8_t ptt = KEYBOARD_IsPttPressed(), ptt2 = KEYBOARD_IsPtt2Pressed();
	if (ptt != last_ptt)
		UI_PrintString(ptt ? "DOWN" : "UP  ", 64, 56, ptt ? RED : GREEN, BLACK, 1);
	if (ptt2 != last_ptt2)
		UI_PrintString(ptt2 ? "DOWN" : "UP  ", 64, 66, ptt2 ? RED : GREEN, BLACK, 1);
	last_ptt = ptt;
	last_ptt2 = ptt2;

	uint16_t rssi = BK4819_GetRSSI();
	if (rssi != last_rssi)
		UI_PrintString(UI_FormatDec(buf, rssi, 3), 64, 90, GREEN, BLACK, 1);
	last_rssi = rssi;

	/* The power-off sense (PB11) is only shown for now, not acted on: with the automatic power-off on, the first
	 * screens on a radio (2026-11-21) kept restarting every few seconds. */
	uint8_t sense = GPIO_CheckBit(PIN_POWER_SENSE);
	if (sense != last_sense)
		UI_PrintString(sense ? "PWR HI" : "PWR LO", 76, 104, sense ? GREEN : RED, BLACK, 1);
	last_sense = sense;
	(void)off_count;
}
