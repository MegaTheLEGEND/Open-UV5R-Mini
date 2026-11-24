/* Open-UV5R-Mini: SPI1, the bus the LCD and the external flash share (clock PB0, data out PB1, data in PB2).
 *
 * The stock app drives both with SPI1's hardware and its DMA: BAUD = clock / Hz - 1 (sub_01E3E3B8, 20 MHz), a DMA
 * send is CON bit 12 clear, ADR = buffer, CNT = length, then wait for CON bit 15 and clear it with bit 14
 * (sub_01E3E430, sub_01E3E3DC). The rest of the register map is from JieLi's public SDK (fw-AC63_BT_SDK, Apache-2.0;
 * facts only, no code taken): include_lib/driver/cpu/br23/asm/br23.h gives SPI1 at 0x1E1D00 (CON, BAUD, BUF, ADR,
 * CNT) and the IO map at 0x1E5118 (CON0..CON5); cpu/br23/spi.c gives the CON bits (0 enable, 1 slave, 2 CS, 3
 * full duplex, 4 sample edge, 5 update edge, 6 clock idle high, 10-11 width, 12 receive, 14 clear pending, 15
 * pending), SPI1 port A = PB0 clock, PB1 out, PB2 in, and the port select in IO map CON1 bit 4 (0 = port A).
 *
 * SPI1_Init checks the hardware path by reading the flash's JEDEC ID through it. If that fails, or a transfer ever
 * times out, SPI1 is switched off for good and the LCD and flash drivers go back to bit-banging the same pins.
 * Not tried on a radio yet (2026-11-23).
 */
#ifndef DRIVER_SPI1_H
#define DRIVER_SPI1_H

#include <stdbool.h>
#include <stdint.h>

#ifndef LCD_HW_SPI
#define LCD_HW_SPI 1        /* features.mk ENABLE_LCD_SPI: 0 keeps the bit-banged bus */
#endif

bool    SPI1_Init(void);                       /* set up and self-test; true if the hardware path is in use */
bool    SPI1_Ok(void);                         /* the hardware path is in use */
void    SPI1_Put(uint8_t out);                 /* send one byte */
uint8_t SPI1_Get(void);                       /* receive one byte (receive mode, as stock reads the flash) */
void    SPI1_Send(const uint8_t *p, uint32_t n);   /* start a DMA send; returns once the previous one is done */
void    SPI1_Wait(void);                       /* wait for a DMA send to finish */

#endif
