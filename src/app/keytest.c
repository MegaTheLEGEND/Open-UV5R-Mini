/* Open-UV5R-Mini: key test (menu item KEY TEST): runs until the radio is switched off.
 *
 * Shows the scan code of the key being pressed, both PTT inputs, the BK4819's REG_00, the RSSI and the
 * Bluetooth state, so the unknown keys and pins can be identified on a real radio. It never transmits.
 * Bytes received over Bluetooth are echoed back (a loopback test for the Bluetooth edition).
 */
#include <stdbool.h>
#include <stdint.h>
#include "app/app.h"
#include "app/keytest.h"
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
static uint16_t bt_rx_count __attribute__((unused));

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
	UI_PrintString("PRESS PTT A", 4, 118, GREY, BLACK, 1);
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

#define TEST_SECTOR 0x40000u   /* inside the app slot, past our small app: only unused leftover bytes */

static uint8_t sector_save[4096];  /* the sector as it was, put back after the test */
static uint8_t sector_buf[4096];
static uint8_t flash_step;         /* PTT A presses so far */

static void show_result(const char *label, uint32_t value, bool good)
{
	char buf[12];

	ST7735S_FillRect(0, 118, 128, 10, BLACK);
	UI_PrintString(label, 4, 118, WHITE, BLACK, 1);
	UI_PrintString(UI_FormatHex(buf, value, 8), 46, 118, good ? GREEN : RED, BLACK, 1);
}

/* Erase, program and read back the test sector, then put the original bytes back. Returns 0 if all went well, or
 * the step that failed (in the top hex digit) and the error or offset. */
static uint32_t flash_write_test(void)
{
	uint32_t r, i;

	SYSTEM_WatchdogClear();
	if ((r = FLASH_Read(TEST_SECTOR, sector_save, sizeof sector_save)) != FLASH_OK)
		return 0x10000000u | (r >> 24);
	for (i = 0; i < sizeof sector_buf; i++)
		sector_buf[i] = (uint8_t)(i ^ (i >> 8) ^ 0x5A);

	SYSTEM_WatchdogClear();
	if ((r = FLASH_Erase4K(TEST_SECTOR)) != FLASH_OK)
		return 0x20000000u | (r >> 24);
	SYSTEM_WatchdogClear();
	if ((r = FLASH_Program(TEST_SECTOR, sector_buf, sizeof sector_buf)) != FLASH_OK)
		return 0x30000000u | (r >> 24);
	SYSTEM_WatchdogClear();
	for (i = 0; i < sizeof sector_buf; i++)
		sector_buf[i] = 0;
	if ((r = FLASH_Read(TEST_SECTOR, sector_buf, sizeof sector_buf)) != FLASH_OK)
		return 0x40000000u | (r >> 24);
	uint32_t bad = 0;
	for (i = 0; i < sizeof sector_buf; i++)
		if (sector_buf[i] != (uint8_t)(i ^ (i >> 8) ^ 0x5A)) {
			bad = 0x50000000u | i;
			break;
		}

	/* put it back as it was, whatever happened above */
	SYSTEM_WatchdogClear();
	if ((r = FLASH_Erase4K(TEST_SECTOR)) != FLASH_OK)
		return 0x60000000u | (r >> 24);
	SYSTEM_WatchdogClear();
	if ((r = FLASH_Program(TEST_SECTOR, sector_save, sizeof sector_save)) != FLASH_OK)
		return 0x70000000u | (r >> 24);
	SYSTEM_WatchdogClear();
	if ((r = FLASH_Read(TEST_SECTOR, sector_buf, sizeof sector_buf)) != FLASH_OK)
		return 0x80000000u | (r >> 24);
	for (i = 0; i < sizeof sector_buf; i++)
		if (sector_buf[i] != sector_save[i])
			return 0x90000000u | i;
	return bad;
}

/* PTT A presses: 1st reads the flash ID and status register (read only); 2nd runs the write test on TEST_SECTOR,
 * which puts the sector back as it was. Nothing outside that sector is written. */
static void flash_test(void)
{
	if (flash_step == 0) {
		uint32_t id = FLASH_ReadId();
		show_result("ID", id, id == 0x856014u);
		char buf[4];
		uint32_t st = FLASH_ReadStatus();
		UI_PrintString("ST", 82, 96, WHITE, BLACK, 1);  /* status reg: block-protect bits would stop writes */
		UI_PrintString(st < 0x100u ? UI_FormatHex(buf, st, 2) : "??", 100, 96, GREEN, BLACK, 1);
		if (id == 0x856014u)
			flash_step = 1;
	} else if (flash_step == 1) {
		show_result("WR..", 0, true);
		uint32_t r = flash_write_test();
		show_result(r ? "WR BAD" : "WR OK", r, r == 0);
		flash_step = 2;
	}
}

static void keytest_init(void)
{
	char buf[8];

	ST7735S_FillRect(0, 0, 128, 128, BLACK);
	BK4819_Init();
	BK4819_SetFrequency(TEST_FREQ_10HZ);
	BK4819_SetMode(BK4819_MODE_RX);
	BT_Init(on_bt_receive);

	draw_static();
	draw_flash_regs();
	UI_PrintString(UI_FormatHex(buf, BK4819_ReadRegister(BK4819_REG_00), 4), 22, 76, GREEN, BLACK, 1);
	UI_PrintString(BT_IsAvailable() ? "ON" : "--", 88, 66, GREY, BLACK, 1);
}

static void keytest_tick(void)
{
	char buf[8];
	KEY_Code_t key = KEYBOARD_Poll();

	/* raw keypad: one hex digit per scan step (no row, then rows PA6..PA9), F = no column low */
	{
		char raw[6];
		for (unsigned s = 0; s < 5; s++) {
			uint8_t c = KEYBOARD_RawColumns(s);
			raw[s] = (char)(c < 10 ? '0' + c : 'A' + c - 10);
		}
		raw[5] = 0;
		UI_PrintString("ROWS", 4, 46, WHITE, BLACK, 1);
		UI_PrintString(raw, 34, 46, GREEN, BLACK, 1);
	}

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
			flash_test();
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

void KEYTEST_Run(void)
{
	keytest_init();
	for (;;) {
		keytest_tick();
		SYSTEM_DelayMs(APP_TICK_MS);
	}
}
