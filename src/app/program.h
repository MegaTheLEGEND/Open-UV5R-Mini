/* Open-UV5R-Mini: programming over the cable in normal mode, as CHIRP's UV-5R Mini driver expects. See program.c. */
#ifndef APP_PROGRAM_H
#define APP_PROGRAM_H

#include <stdint.h>

#define PROGRAM_BAUD 115200u

/* First radio try (2026-11-22): the screen went white with CHIRP on. Cause: UART1's own fixed pins (0x1E5124 bit 7)
 * were left on, so the UART also drove PB0/PB1; UART_FixedPinsOff clears it (docs/RE-NOTES.md). Confirmed on a radio
 * (Sean, 2026-11-22: the screen works with CHIRP on). */
/* Switched in features.mk (ENABLE_CHIRP, ENABLE_SCREEN_TEST); these defaults are for builds without it. */
#ifndef PROGRAM_ENABLE
#define PROGRAM_ENABLE 1
#endif
#ifndef PROGRAM_SCREEN_TEST
#define PROGRAM_SCREEN_TEST 0
#endif

/* The same programming over Bluetooth SPP ("Wireless Cps"): only in the Bluetooth edition, whose SDK build sets it
 * (docs/BLUETOOTH.md). The open build has no Bluetooth, so 0. */
#ifndef PROGRAM_BT
#define PROGRAM_BT 0
#endif

enum { PROGRAM_IDLE, PROGRAM_DONE_READ, PROGRAM_DONE_WRITTEN };

#if PROGRAM_BT
/* Bytes received over Bluetooth SPP. Give this to BT_Init; it only queues them (any task), PROGRAM_Poll reads them. */
void PROGRAM_BtReceive(const uint8_t *data, uint16_t len);
#endif

/* Set up the cable UART at 115200 (call once at start-up). */
void PROGRAM_Init(void);

/* Call every main-loop tick. When the programming software says hello (on the cable, or with PROGRAM_BT over
 * Bluetooth), this runs the whole session on that link (blocking, with the PROGRAM screen) and returns how it ended:
 * PROGRAM_DONE_WRITTEN means the channels or settings changed and the app must reload them. */
int  PROGRAM_Poll(void);

#endif
