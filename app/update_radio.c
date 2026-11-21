/* Open-UV5R-Mini: update mode on the radio: entry check, UPDATE... screen, and the UPDATE_Port for app/update.c.
 *
 * NOT TESTED ON A RADIO YET. The cable UART is routed to the USB pins as stock does (driver/uart.c), but its baud
 * rate assumes the 48 MHz clock the stock app sets up, and our start-up doesn't set clocks yet. tools/mkufw.py
 * refuses a flashable image of an app without the stock update mode. docs/UPDATE-MODE.md has the status.
 *
 * Internal flash goes through our own RAM-code driver (driver/flash.c: raw SPI read, 4 KB erase, page program),
 * which worked on a radio. A failed write or read-back makes the store refuse (result 0x86): the loader area is the
 * free top of the settings area, so a failure there leaves the radio in update mode, flashable.
 *
 * The hand-over does what the stock one does: interrupts off and 0x10F1A8 = 6 (stock 0x020012BE), the record at
 * 0x2FF88, the RAM protection opened, P33 register 0x32 set (stock 0x0200083A), then app/handover.S from RAM.
 */
#include <stdint.h>
#include "app/update.h"
#include "bsp/ac695n.h"
#include "driver/flash.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/st7735s.h"
#include "ui/helper.h"
#include "board.h"

void UPDATE_JumpToBoot(void);   /* app/handover.S, in RAM; does not return */

int UPDATE_Requested(void)
{
	/* stock sub_01E492C0: '=' column (PC2) and PTT-A (PA4) both low at power-on */
	return !GPIO_CheckBit(PIN_KEY_COL0) && !GPIO_CheckBit(PIN_PTT);
}

/* Debug lines on the UPDATE... screen (first cable tests on a radio, 2026-11-21): the UART state, the bytes that
 * reached the update protocol and the frames it sent, so a stall shows where it is. */
static uint32_t s_rx_total, s_tx_frames;
static uint8_t s_last[8];

static void debug_draw(void)
{
	char b[12];

	UI_PrintString("C", 4, 88, 0xFFFF, 0x0000, 1);
	UI_PrintString(UI_FormatHex(b, UART_Reg(0), 8), 16, 88, 0x07E0, 0x0000, 1);
	UI_PrintString("H", 70, 88, 0xFFFF, 0x0000, 1);
	UI_PrintString(UI_FormatHex(b, UART_Reg(40), 4), 82, 88, 0x07E0, 0x0000, 1);
	UI_PrintString("RX", 4, 98, 0xFFFF, 0x0000, 1);
	UI_PrintString(UI_FormatDec(b, s_rx_total % 10000u, 4), 22, 98, 0x07E0, 0x0000, 1);
	UI_PrintString("TX", 58, 98, 0xFFFF, 0x0000, 1);
	UI_PrintString(UI_FormatDec(b, s_tx_frames % 10000u, 4), 76, 98, 0x07E0, 0x0000, 1);
	for (int i = 0; i < 8; i++)
		UI_PrintString(UI_FormatHex(b, s_last[i], 2), (uint8_t)(4 + 15 * i), 108, 0xFFE0, 0x0000, 1);
}

static int port_read(uint8_t *buf, int max, uint32_t timeout_ms)
{
	/* wait in slices so the screen can show the state while nothing comes */
	for (uint32_t waited = 0;;) {
		uint32_t slice = timeout_ms - waited < 300u ? timeout_ms - waited : 300u;
		int n = UART_Receive(buf, max, slice);
		if (n > 0) {
			for (int i = 0; i < n; i++) {
				for (int j = 0; j < 7; j++)
					s_last[j] = s_last[j + 1];
				s_last[7] = buf[i];
			}
			s_rx_total += (uint32_t)n;
			debug_draw();
			return n;
		}
		waited += slice;
		debug_draw();
		if (waited >= timeout_ms)
			return 0;
	}
}

static void port_write(const uint8_t *buf, int len)
{
	s_tx_frames++;
	debug_draw();
	UART_Send(buf, (uint32_t)len);
}
static void port_baud(uint32_t baud) { UART_SetBaud(baud); }

static int port_flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
	return FLASH_Read(addr, buf, len) == FLASH_OK ? 0 : -1;
}

static int port_flash_erase_4k(uint32_t addr)
{
	if (addr < UPDATE_STORE_LO || addr >= UPDATE_VM_END || (addr & 0xFFFu))
		return -1;               /* only ever the free top of the settings area */
	return FLASH_Erase4K(addr) == FLASH_OK ? 0 : -1;
}

static int port_flash_program(uint32_t addr, const uint8_t *buf, uint32_t len)
{
	if (addr < UPDATE_STORE_LO || addr + len > UPDATE_VM_END || (addr & 0xFFu) + len > 0x100u)
		return -1;
	return FLASH_Program(addr, buf, len) == FLASH_OK ? 0 : -1;
}

/* The hardware lock at 0x101044: each 0xE7 written flips bit 0 (stock sub_01E430D2 / sub_01E430E6). */
static void lock_take(void) { do REG32(0x101044u) = 0xE7u; while (!(REG32(0x101044u) & 1u)); }
static void lock_give(void) { do REG32(0x101044u) = 0xE7u; while (REG32(0x101044u) & 1u); }

/* Open the RAM protection, as the stock hand-over does (sub_01E43130 with sub_01E430FA and sub_01E43110). */
static void ram_protect_close(void)
{
	lock_take();
	REG32(0x101000u) &= 0x80FFFFFFu;
	REG32(0x101040u) = 0;
	REG32(0x10F0D0u) = 3;
	lock_take(); REG32(0x101074u) = 0; REG32(0x101070u) = 0x01FFFFFFu; lock_give();
	lock_take();
	REG32(0x10107Cu) = 0;
	REG32(0x101078u) = 0x01FFFFFFu;
	lock_give();
	lock_take(); REG32(0x10F0E0u) = 0x01FFFFFFu; REG32(0x10F0E4u) = 0; lock_give();
	lock_take(); REG32(0x10F0D8u) = 0x01FFFFFFu; REG32(0x10F0DCu) = 0; lock_give();
	lock_give();
}

static void port_handover(const uint8_t *record, uint32_t len)
{
	volatile uint8_t *dst = (volatile uint8_t *)(uintptr_t)UPDATE_RECORD_ADDR;

	__asm__ volatile("cli");
	REG32(0x0010F1A8u) = 6;              /* as stock 0x020012BE */
	for (uint32_t i = 0; i < len; i++)
		dst[i] = record[i];
	ram_protect_close();
	P33_Write(0x32, (REG32(0x001E511Cu) & 0x20u) ? 8 : 4);   /* as stock 0x0200083A */
	UPDATE_JumpToBoot();
	for (;;)
		;
}

static const UPDATE_Port s_port = {
	port_read, port_write, port_baud,
	port_flash_read, port_flash_erase_4k, port_flash_program,
	port_handover,
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
