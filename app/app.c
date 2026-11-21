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
#include "bsp/ac695n.h"
#include "driver/bk4819.h"
#include "driver/flash.h"
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
static uint16_t off_count;  /* ticks the power switch has read "off" */
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
	UI_PrintString("BT", 70, 66, WHITE, BLACK, 1);
	UI_PrintString("BK", 4, 76, WHITE, BLACK, 1);
	UI_PrintString("RSSI", 58, 76, WHITE, BLACK, 1);
	UI_PrintString("ID  PRESS PTT A", 4, 118, GREY, BLACK, 1);
}

/* Read-only look at the flash controller, SPI0 and the two possible flash chip-select pins, for the flash driver
 * (docs/UPDATE-MODE.md). Only reads registers. */
static void draw_flash_regs(void)
{
	char buf[12];
	const GPIO_Port_t *pa = GPIOA, *pd = GPIOD;

	UI_PrintString("SF", 4, 86, WHITE, BLACK, 1);
	UI_PrintString(UI_FormatHex(buf, REG32(0x001F0200u), 8), 22, 86, GREEN, BLACK, 1);
	UI_PrintString(UI_FormatHex(buf, REG32(0x001F0204u), 4), 76, 86, GREEN, BLACK, 1);
	UI_PrintString("SP", 4, 96, WHITE, BLACK, 1);
	UI_PrintString(UI_FormatHex(buf, REG32(0x001E1C00u), 8), 22, 96, GREEN, BLACK, 1);
	UI_PrintString("PD", 4, 106, WHITE, BLACK, 1);
	UI_PrintString(UI_FormatHex(buf, pd->OUT & 0xFFFFu, 4), 22, 106, GREEN, BLACK, 1);
	UI_PrintString(UI_FormatHex(buf, pd->DIR & 0xFFFFu, 4), 50, 106, GREEN, BLACK, 1);
	UI_PrintString("A", 82, 106, WHITE, BLACK, 1);
	UI_PrintString((pa->OUT >> 13) & 1u ? "1" : "0", 94, 106, GREEN, BLACK, 1);
	UI_PrintString((pa->DIR >> 13) & 1u ? "1" : "0", 106, 106, GREEN, BLACK, 1);
}

static bool id_bad(uint32_t id)
{
	return id >= 0x01000000u || id == 0 || id == 0xFFFFFFu;
}

/* On a PTT A press: read the flash's JEDEC ID from RAM code. Try PA13 as chip select first (an output held high,
 * seen on a radio), PD3 only if that gets no answer. Nothing is written to the flash. */
static void flash_id_test(void)
{
	char buf[12];
	const char *pin = "A13";
	uint32_t id = FLASH_ReadId(FLASH_CS_PA13);

	if (id_bad(id) && id != FLASH_ERR_CONTINUOUS) {
		uint32_t id2 = FLASH_ReadId(FLASH_CS_PD3);
		if (!id_bad(id2)) {
			id = id2;
			pin = "D3 ";
		} else
			pin = "-- ";
	}
	UI_PrintString("ID ", 4, 118, WHITE, BLACK, 1);
	UI_PrintString(pin, 22, 118, WHITE, BLACK, 1);
	UI_PrintString(UI_FormatHex(buf, id, 8), 46, 118, id_bad(id) ? RED : GREEN, BLACK, 1);
	UI_PrintString("   ", 94, 118, GREY, BLACK, 1);
	draw_flash_regs();      /* the registers again, after the test */
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
	draw_flash_regs();
	UI_PrintString(UI_FormatHex(buf, BK4819_ReadRegister(BK4819_REG_00), 4), 22, 76, GREEN, BLACK, 1);
	UI_PrintString(BT_IsAvailable() ? "ON" : "--", 88, 66, GREY, BLACK, 1);
}

void APP_Tick(void)
{
	char buf[8];
	KEY_Code_t key = KEYBOARD_Poll();

	static uint8_t last_ptt = 0xFF, last_ptt2 = 0xFF;
	static uint16_t last_rssi = 0xFFFF;

	SYSTEM_WatchdogClear();

	/* Power switch: PB11 high means turned off (stock sub_01E4C978 checks the same, with a short debounce).
	 * Off after it reads high for 0.2 s. */
	if (GPIO_CheckBit(PIN_POWER_SENSE)) {
		if (++off_count >= 200 / APP_TICK_MS)
			BOARD_PowerOff();
	} else
		off_count = 0;

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
	if (ptt != last_ptt) {
		UI_PrintString(ptt ? "DOWN" : "UP  ", 40, 56, ptt ? RED : GREEN, BLACK, 1);
		if (ptt && last_ptt == 0)
			flash_id_test();
	}
	if (ptt2 != last_ptt2)
		UI_PrintString(ptt2 ? "DOWN" : "UP  ", 40, 66, ptt2 ? RED : GREEN, BLACK, 1);
	last_ptt = ptt;
	last_ptt2 = ptt2;

	uint16_t rssi = BK4819_GetRSSI();
	if (rssi != last_rssi)
		UI_PrintString(UI_FormatDec(buf, rssi, 3), 88, 76, GREEN, BLACK, 1);
	last_rssi = rssi;
}
