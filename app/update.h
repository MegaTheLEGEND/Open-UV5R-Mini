/* Open-UV5R-Mini: cable update mode (the stock app's UART update stage, reimplemented).
 *
 * Power on holding PTT-A and '=': the radio shows UPDATE... and waits for the programming software. This is the
 * stage that must exist in every app, or a radio running it can never be flashed again (docs/TESTING.md).
 *
 * The protocol, as the stock v0.27 app runs it (docs/UPDATE-MODE.md has the reverse-engineering notes):
 *   frames  AA 55 | len u16 | cmd u8 | data | crc16-xmodem u16      (len = 1 + data length, little-endian)
 *   host -> radio  6 (ping, empty)          radio -> host  1 (empty: "update mode here")
 *   host -> radio  1 {baud u32}             radio: a new rate is set and echoed; the current rate starts the update
 *   radio -> host  2 {off u32, len u32}     host -> radio  2 {off, len, bytes}: a slice of the .ufw
 *   radio -> host  3 {code}                 the result (only on failure in this stage)
 * The radio reads the .ufw header and records, finds ota.bin, fetches the loader (uart_user.bin) and checks its
 * CRCs, stores it, writes the hand-over record for the bootloader at RAM 0x2FF88 and resets into the loader.
 *
 * Hardware-independent: everything the radio does goes through UPDATE_Port, so the same code runs on the radio and
 * on a PC against the real flasher (tests/update_host.py).
 */
#ifndef APP_UPDATE_H
#define APP_UPDATE_H

#include <stdint.h>

#define UPDATE_BAUD_DEFAULT   9600u
#define UPDATE_MAX_LOADER     0x4000u       /* the pinned loaders are 15,648 and 15,848 bytes packed */
#define UPDATE_RECORD_ADDR    0x2FF88u      /* UPDATA_FLAG_ADDR in the stock app (UPDATA_BEG 0x2FF80 + 8) */
#define UPDATE_RECORD_SIZE    112u
#define UPDATE_PIN_TX         61u           /* USB D+ used as UART TX (stock table at 0x01E65538) */
#define UPDATE_PIN_RX         62u           /* USB D- used as UART RX */

/* Where the loader goes on an nrf-v0.27 radio (VM area 0x94000 + 0x6A000, settings banks 0x94000..0xA4000).
 * Learnt by running the stock allocator sub_01E42AF2 in the emulator (emu/loader_store.py, docs/UPDATE-MODE.md):
 * the loader sits at the top of the VM area, its 32-byte LOADER.BIN header 4 bytes below it, and a flash copy of
 * the hand-over record in the last 256 bytes before BTIF (0xFE000). */
#define UPDATE_VM_END         0xFE000u
#define UPDATE_REC_FLASH      (UPDATE_VM_END - 0x100u)               /* 0xFDF00 */
#define UPDATE_STORE_LO       0xFA000u      /* erased before the store: free top of the VM area */
#define UPDATE_SETTINGS_END   0xA4000u      /* the settings banks end here: never touched */

enum {
	UPDATE_CMD_RATE   = 1,
	UPDATE_CMD_READ   = 2,
	UPDATE_CMD_RESULT = 3,
	UPDATE_CMD_LENGTH = 4,
	UPDATE_CMD_PING   = 6,
};

/* Results reported with UPDATE_CMD_RESULT, the same codes the stock app uses. */
enum {
	UPDATE_OK            = 0x00,
	UPDATE_ERR_FILE      = 0x83,   /* not a file for this radio: bad header, chip, CRC or no loader */
	UPDATE_ERR_STORE     = 0x86,   /* the loader could not be stored */
	UPDATE_ERR_HOST      = 0x87,   /* the host stopped answering */
};

typedef struct {
	/* Up to max bytes received within timeout_ms; 0 on time-out. */
	int  (*read)(uint8_t *buf, int max, uint32_t timeout_ms);
	void (*write)(const uint8_t *buf, int len);
	void (*set_baud)(uint32_t baud);
	/* Internal flash (absolute flash addresses). 0 = done. Program never crosses a 256-byte page. */
	int  (*flash_read)(uint32_t addr, uint8_t *buf, uint32_t len);
	int  (*flash_erase_4k)(uint32_t addr);
	int  (*flash_program)(uint32_t addr, const uint8_t *buf, uint32_t len);
	/* Put the 112-byte record at UPDATE_RECORD_ADDR and reset. Does not return on the radio. */
	void (*handover)(const uint8_t *record, uint32_t len);
} UPDATE_Port;

/* PTT-A (PA4) and '=' (column PC2) held at power-on, as the stock app checks them (sub_01E492C0). */
int  UPDATE_Requested(void);

/* On the radio: show UPDATE... and serve the cable until the hand-over. Never returns. */
void UPDATE_Mode(void);

/* Run update mode until the hand-over (no return on the radio) or a failure (returns its result code). */
int  UPDATE_Run(const UPDATE_Port *port);

/* Helpers, exported for tests. */
uint16_t UPDATE_Crc16(const uint8_t *p, uint32_t n, uint16_t crc);
void     UPDATE_Decrypt(uint8_t *p, uint32_t n, uint16_t key);
uint32_t UPDATE_StoreLoader(const uint8_t *loader, uint32_t len);
void     UPDATE_BuildRecord(uint8_t rec[UPDATE_RECORD_SIZE], uint32_t loader_addr, uint32_t baud);

#endif
