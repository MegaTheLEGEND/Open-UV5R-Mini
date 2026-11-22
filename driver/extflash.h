/* Open-UV5R-Mini: the external SPI flash (Giantec GT25Q16A, 2 MB, ID C46015).
 *
 * It holds the channels (0x0000), settings (0x9000), the radio's own calibration (0xF000) and the fonts and pictures
 * (RE-NOTES, "External SPI flash"). The calibration exists nowhere else. Read only by default: the erase and program
 * commands are compiled in only with SETTINGS_ENABLE_SAVE 1 (app/settings.h), and even then they refuse anything
 * outside the two stock settings logs, 0x8000-0x9FFF (EXTFLASH_LOG_LO/HI), and read every program back. Bit-banged on CS PA3, SCK PB0, MOSI PB1, MISO PB2 (SPI1 port A, shared with the LCD, whose CS PB5 stays high),
 * the same way as tools/jl_extflash, which read a whole radio's flash in rescue mode.
 */
#ifndef DRIVER_EXTFLASH_H
#define DRIVER_EXTFLASH_H

#include <stdint.h>

#define EXTFLASH_ID 0xC46015u

void     EXTFLASH_Init(void);
uint32_t EXTFLASH_ReadId(void);
void     EXTFLASH_Read(uint32_t addr, uint8_t *buf, uint32_t len);

#define EXTFLASH_LOG_LO 0x8000u      /* the only area that may be erased or programmed */
#define EXTFLASH_LOG_HI 0xA000u

/* 0 = done; -1 = refused (outside the fence, wrong chip) or failed (busy too long, read-back differs).
 * Program may cross 256-byte pages; it is split as stock does (sub_01E40526). Erase is one 4 KB sector. */
int      EXTFLASH_LogErase4K(uint32_t addr);
int      EXTFLASH_LogProgram(uint32_t addr, const uint8_t *buf, uint32_t len);

#endif
