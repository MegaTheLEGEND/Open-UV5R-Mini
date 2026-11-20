/* Open-UV5R-Mini: update mode on the radio: entry check, UPDATE... screen, and the UPDATE_Port for app/update.c.
 *
 * NOT FLASHABLE YET. Two pieces are still open (docs/UPDATE-MODE.md):
 *   1. store_loader: where and how the loader is written to flash. The stock app does this in JieLi's closed
 *      update library (0x01E686C4..0x01E69ECC), and the address it passes as ota_addr is not known yet.
 *      Until then it refuses (result 0x86) and the radio stays in update mode: nothing is erased or written.
 *   2. The USB-pin IO routing for the UART (stock: sub_01E001D2.. on pins 61/62, IOMAP sub_01E4856C/sub_01E484B6).
 * tools/mkufw.py also refuses to make a flashable image of an app without the stock update mode.
 */
#include <stdint.h>
#include "app/update.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/st7735s.h"
#include "ui/helper.h"
#include "board.h"

int UPDATE_Requested(void)
{
	/* stock sub_01E492C0: '=' column (PC2) and PTT-A (PA4) both low at power-on */
	return !GPIO_CheckBit(PIN_KEY_COL0) && !GPIO_CheckBit(PIN_PTT);
}

static int port_read(uint8_t *buf, int max, uint32_t timeout_ms) { return UART_Receive(buf, max, timeout_ms); }
static void port_write(const uint8_t *buf, int len) { UART_Send(buf, (uint32_t)len); }
static void port_baud(uint32_t baud) { UART_SetBaud(baud); }

static int port_store_loader(const uint8_t *loader, uint32_t len, uint32_t *flash_addr)
{
	(void)loader;
	(void)len;
	(void)flash_addr;
	return -1;   /* open item 1: refuse rather than guess an address */
}

static void port_handover(const uint8_t *record, uint32_t len)
{
	volatile uint8_t *dst = (volatile uint8_t *)(uintptr_t)UPDATE_RECORD_ADDR;
	for (uint32_t i = 0; i < len; i++)
		dst[i] = record[i];
	for (;;)
		;            /* reset into the bootloader: not reached until store_loader is done */
}

static const UPDATE_Port s_port = {
	port_read, port_write, port_baud, port_store_loader, port_handover,
};

void UPDATE_Mode(void)
{
	ST7735S_FillRect(0, 0, 128, 128, 0x0000);
	UI_PrintString("UPDATE...", 28, 56, 0xFFFF, 0x0000, 1);
	UART_Init(UPDATE_BAUD_DEFAULT);
	for (;;) {
		int r = UPDATE_Run(&s_port);
		char msg[8];
		UI_PrintString("ERR ", 28, 76, 0xF800, 0x0000, 1);
		UI_PrintString(UI_FormatHex(msg, (uint32_t)r, 2), 60, 76, 0xF800, 0x0000, 1);
	}
}
