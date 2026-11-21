/* Open-UV5R-Mini: board pin map (Baofeng UV-5R Mini, stock v0.27). See docs/RE-NOTES.md. */
#ifndef BOARD_H
#define BOARD_H

#include "bsp/ac695n.h"

/* BK4819 3-wire bus (bit-banged) */
#define PIN_BK4819_SCK   IO_PORTA(10)
#define PIN_BK4819_SDATA IO_PORTC(0)
#define PIN_BK4819_SCN   IO_PORTC(1)

/* ST7735S LCD */
#define PIN_LCD_CS       IO_PORTB(5)
#define PIN_LCD_RESET    IO_PORTB(4)
#define PIN_LCD_BACKLIGHT IO_PORTB(7)   /* active high */
#define PIN_LCD_SCK      IO_PORTB(0)    /* SPI1 port A clock (confirmed by the BR23 SDK pin table) */
#define PIN_LCD_MOSI     IO_PORTB(1)    /* SPI1 port A data out (confirmed) */
#define LCD_DC_PR_BIT    1              /* D/C is PR1 in the power domain */

/* Keypad: rows driven low one at a time, columns read with pull-ups */
#define PIN_KEY_ROW0     IO_PORTA(6)
#define PIN_KEY_ROW1     IO_PORTA(7)
#define PIN_KEY_ROW2     IO_PORTA(8)
#define PIN_KEY_ROW3     IO_PORTA(9)
#define PIN_KEY_COL0     IO_PORTC(2)
#define PIN_KEY_COL1     IO_PORTC(3)
#define PIN_KEY_COL2     IO_PORTC(4)
#define PIN_KEY_COL3     IO_PORTC(5)

/* PTT and power */
#define PIN_PTT          IO_PORTA(4)    /* main PTT (to confirm on hardware) */
#define PIN_PTT2         IO_PORTA(12)   /* second PTT / VFO B (to confirm) */
#define PIN_POWER_HOLD   IO_PORTB(10)   /* high keeps the radio on */
#define PIN_POWER_SENSE  IO_PORTB(11)   /* high = power switch turned off (stock sub_01E4C978) */

/* Audio, LEDs, lamp and band switch: candidates from the stock code, not yet confirmed on a radio
 * (docs/RE-NOTES.md, "Peripheral sweep") */
#define PIN_SPEAKER_AMP  IO_PORTB(8)    /* high = amplifier on (stock sub_01E400D6) */
#define PIN_LED_RX       IO_PORTB(9)    /* green RX LED (stock sub_01E40CE0) */
#define PIN_LAMP         IO_PORTB(6)    /* flashlight (stock sub_01E41906) */
#define PIN_RX_FRONT     IO_PORTC(7)    /* receive front end: high below 300 MHz (stock sub_01E41382) */
#define LED_TX_PR_BIT    0              /* red TX LED on PR0 (stock sub_01E418E0) */

void BOARD_Init(void);
void BOARD_PowerOff(void);

#endif
