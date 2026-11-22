/* Open-UV5R-Mini: the screen's colours, in one place so every screen looks the same (RGB565). */
#ifndef UI_THEME_H
#define UI_THEME_H

#include "driver/st7735s.h"

#define COLOR_BG       RGB565(0, 0, 0)          /* screen background */
#define COLOR_BAR      RGB565(22, 26, 36)       /* status bar and panel rules */
#define COLOR_TEXT     RGB565(255, 255, 255)    /* main text: the active side's frequency */
#define COLOR_DIM      RGB565(110, 115, 125)    /* secondary text, the side not in use, unlit icons */
#define COLOR_FAINT    RGB565(45, 48, 56)       /* unlit meter bars */
#define COLOR_ACCENT   RGB565(40, 130, 255)     /* the active side's badge, selections */
#define COLOR_INFO     RGB565(120, 200, 255)    /* tags: modulation, bandwidth, tones */
#define COLOR_RX       RGB565(0, 220, 80)       /* receiving */
#define COLOR_TX       RGB565(255, 40, 40)      /* transmitting, errors */
#define COLOR_WARN     RGB565(255, 200, 0)      /* typing, lamp, warnings */

#endif
