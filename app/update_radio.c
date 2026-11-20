/* Open-UV5R-Mini: update mode on the radio: entry check, UPDATE... screen, and the UPDATE_Port for app/update.c.
 *
 * NOT FLASHABLE YET: the UART is not yet routed to the USB pins (stock: sub_01E001D2.. on pins 61/62, IOMAP
 * sub_01E4856C/sub_01E484B6), and nothing here has run on hardware. tools/mkufw.py refuses a flashable image of an
 * app without the stock update mode. docs/UPDATE-MODE.md has the status.
 *
 * Internal flash goes through the same mask-ROM routines the stock flash driver uses (sub_01E421F6, sub_01E4256C,
 * sub_01E42264): read 0x0200048C(buf, addr, len), page program 0x02000772(buf, addr, len, 0), erase
 * 0x020006BC(0xC8 = 4 KiB sector, addr). A failed write or read-back makes the store refuse (result 0x86): the loader
 * area is the free top of the settings area, so a failure there leaves the radio in update mode, flashable.
 */
#include <stdint.h>
#include "app/update.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/st7735s.h"
#include "ui/helper.h"
#include "board.h"

#define ROM(addr, type) ((type)(uintptr_t)(addr))
typedef int  (*rom_read_t)(void *buf, uint32_t addr, uint32_t len);
typedef int  (*rom_prog_t)(const void *buf, uint32_t addr, uint32_t len, uint32_t zero);
typedef int  (*rom_erase_t)(uint32_t cmd, uint32_t addr);
typedef uint32_t (*rom_irq_save_t)(void);
typedef void (*rom_void_t)(void);

#define ROM_SFC_READ    ROM(0x0200048Cu, rom_read_t)
#define ROM_SFC_PROG    ROM(0x02000772u, rom_prog_t)
#define ROM_SFC_ERASE   ROM(0x020006BCu, rom_erase_t)
#define ROM_IRQ_SAVE    ROM(0x020012BEu, rom_irq_save_t)
#define ROM_CLOSE       ROM(0x0200083Au, rom_void_t)      /* called by the stock hand-over before the jump */
#define ROM_JUMP        ROM(0x020011E6u, rom_void_t)      /* the stock hand-over ends here (reset into the boot) */

#ifndef REG32
#define REG32(a)        (*(volatile uint32_t *)(uintptr_t)(a))
#endif

int UPDATE_Requested(void)
{
	/* stock sub_01E492C0: '=' column (PC2) and PTT-A (PA4) both low at power-on */
	return !GPIO_CheckBit(PIN_KEY_COL0) && !GPIO_CheckBit(PIN_PTT);
}

static int port_read(uint8_t *buf, int max, uint32_t timeout_ms) { return UART_Receive(buf, max, timeout_ms); }
static void port_write(const uint8_t *buf, int len) { UART_Send(buf, (uint32_t)len); }
static void port_baud(uint32_t baud) { UART_SetBaud(baud); }

static int port_flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
	ROM_SFC_READ(buf, addr, len);
	return 0;
}

static int port_flash_erase_4k(uint32_t addr)
{
	if (addr < UPDATE_STORE_LO || addr >= UPDATE_VM_END || (addr & 0xFFFu))
		return -1;               /* only ever the free top of the settings area */
	ROM_SFC_ERASE(0xC8u, addr);
	return 0;
}

static int port_flash_program(uint32_t addr, const uint8_t *buf, uint32_t len)
{
	if (addr < UPDATE_STORE_LO || addr + len > UPDATE_VM_END || (addr & 0xFFu) + len > 0x100u)
		return -1;
	ROM_SFC_PROG(buf, addr, len, 0);
	return 0;
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

	(void)ROM_IRQ_SAVE();
	for (uint32_t i = 0; i < len; i++)
		dst[i] = record[i];
	ram_protect_close();
	ROM_CLOSE();
	ROM_JUMP();
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
