/* Open-UV5R-Mini: internal flash access, run from RAM (driver/flash.c). */
#ifndef DRIVER_FLASH_H
#define DRIVER_FLASH_H

#include <stdint.h>

/* Results. Errors have the top byte set, so they can't be a 24-bit ID or a status byte. */
#define FLASH_OK             0u
#define FLASH_ERR_CONTINUOUS 0xE1000000u /* the controller is in continuous-read mode: not tried */
#define FLASH_ERR_TIMEOUT    0xE2000000u
#define FLASH_ERR_ADDR       0xE3000000u /* outside the two writable areas: refused */
#define FLASH_ERR_WREN       0xE4000000u /* write enable didn't take */

uint32_t FLASH_ReadId(void);       /* JEDEC ID (0x9F): 0x856014 on a radio */
uint32_t FLASH_ReadStatus(void);   /* status register 1 (0x05) */
/* Raw reads and writes through SPI: the bytes as stored, so still encrypted where the stock layout encrypts them. */
uint32_t FLASH_Read(uint32_t addr, uint8_t *buf, uint32_t len);
uint32_t FLASH_Erase4K(uint32_t addr);                                   /* 0x1000..0x7C000, 0xFA000..0xFE000 only */
uint32_t FLASH_Program(uint32_t addr, const uint8_t *buf, uint32_t len); /* 0x1000..0x7C000, 0xFA000..0xFE000 only */

#endif
