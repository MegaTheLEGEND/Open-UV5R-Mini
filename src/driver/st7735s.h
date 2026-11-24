/* Open-UV5R-Mini: ST7735S 128x128 RGB565 LCD. */
#ifndef DRIVER_ST7735S_H
#define DRIVER_ST7735S_H

#include <stdbool.h>
#include <stdint.h>

#define LCD_WIDTH  128
#define LCD_HEIGHT 128

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

void ST7735S_Init(void);
void ST7735S_SetBacklight(bool on);
void ST7735S_FillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t color);
void ST7735S_SetWindow(uint8_t x, uint8_t y, uint8_t w, uint8_t h);
void ST7735S_PushPixel(uint16_t color);
void ST7735S_End(void);            /* after the pixels of a window: send what is left, CS high */
bool ST7735S_HardwareSpi(void);    /* SPI1 with DMA is in use (else bit-banged) */

#endif
