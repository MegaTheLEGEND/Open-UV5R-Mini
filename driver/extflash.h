/* Open-UV5R-Mini: the external SPI flash (Giantec GT25Q16A, 2 MB, ID C46015), READ ONLY.
 *
 * It holds the channels (0x0000), settings (0x9000), the radio's own calibration (0xF000) and the fonts and pictures
 * (RE-NOTES, "External SPI flash"). The calibration exists nowhere else, so this driver has no erase or write code at
 * all. Bit-banged on CS PA3, SCK PB0, MOSI PB1, MISO PB2 (SPI1 port A, shared with the LCD, whose CS PB5 stays high),
 * the same way as tools/jl_extflash, which read a whole radio's flash in rescue mode.
 */
#ifndef DRIVER_EXTFLASH_H
#define DRIVER_EXTFLASH_H

#include <stdint.h>

#define EXTFLASH_ID 0xC46015u

void     EXTFLASH_Init(void);
uint32_t EXTFLASH_ReadId(void);
void     EXTFLASH_Read(uint32_t addr, uint8_t *buf, uint32_t len);

#endif
