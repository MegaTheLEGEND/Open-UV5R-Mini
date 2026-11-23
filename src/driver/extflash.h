/* Open-UV5R-Mini: the external SPI flash (Giantec GT25Q16A, 2 MB, ID C46015).
 *
 * It holds the channels (0x0000), settings (0x9000), the radio's own calibration (0xF000) and the fonts and pictures
 * (RE-NOTES, "External SPI flash"). The calibration exists nowhere else. The erase and program commands are compiled
 * in only with SETTINGS_ENABLE_SAVE 1 (store/settings.h), and refuse anything outside 0x0000-0xAFFF
 * (EXTFLASH_WRITE_LO/HI: channels, the two stock settings logs, the DTMF block at 0xA000, all that CHIRP writes), so
 * the calibration at 0xF000 and the fonts can never be erased. Every program is read back. Bit-banged on CS PA3, SCK PB0, MOSI PB1, MISO PB2 (SPI1 port A, shared with the LCD, whose CS PB5 stays high),
 * the same way as tools/jl_extflash, which read a whole radio's flash in rescue mode.
 */
#ifndef DRIVER_EXTFLASH_H
#define DRIVER_EXTFLASH_H

#include <stdint.h>

#define EXTFLASH_ID 0xC46015u

void     EXTFLASH_Init(void);
uint32_t EXTFLASH_ReadId(void);
void     EXTFLASH_Read(uint32_t addr, uint8_t *buf, uint32_t len);

#define EXTFLASH_WRITE_LO 0x0000u      /* the only area that may be erased or programmed */
#define EXTFLASH_WRITE_HI 0xB000u

/* 0 = done; -1 = refused (outside the fence, wrong chip) or failed (busy too long, read-back differs).
 * Program may cross 256-byte pages; it is split as stock does (sub_01E40526). Erase is one 4 KB sector. */
int      EXTFLASH_Erase4K(uint32_t addr);
int      EXTFLASH_Program(uint32_t addr, const uint8_t *buf, uint32_t len);

/* Why the last erase or program failed (shown on the screen when a save fails), and the status register (bits 2-4
 * block-protect: set, the chip ignores erases and programs). */
enum { EXTFLASH_ERR_NONE, EXTFLASH_ERR_FENCE, EXTFLASH_ERR_ID, EXTFLASH_ERR_BUSY, EXTFLASH_ERR_ERASE,
       EXTFLASH_ERR_VERIFY };
uint8_t  EXTFLASH_LastError(void);
uint8_t  EXTFLASH_ReadStatus(void);

#endif
