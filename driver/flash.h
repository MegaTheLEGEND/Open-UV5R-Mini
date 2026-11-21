/* Open-UV5R-Mini: internal flash access, run from RAM. */
#ifndef DRIVER_FLASH_H
#define DRIVER_FLASH_H

#include <stdint.h>

/* Chip-select pins seen in the stock flash driver. */
#define FLASH_CS_PD3  0u
#define FLASH_CS_PA13 1u

/* Results of FLASH_ReadId other than an ID (top byte set, so they can't be a 24-bit ID). */
#define FLASH_ERR_CONTINUOUS 0xE1000000u /* the controller is in continuous-read mode: not tried */
#define FLASH_ERR_TIMEOUT    0xE2000000u

/* Read the flash's JEDEC ID (command 0x9F) with the flash controller paused, through SPI0 and the given CS pin.
 * Read only: nothing is written to the flash. */
uint32_t FLASH_ReadId(uint32_t cs);

#endif
