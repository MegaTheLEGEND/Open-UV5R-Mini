/* Open-UV5R-Mini: programming over the cable in normal mode, as CHIRP's UV-5R Mini driver expects. See program.c. */
#ifndef APP_PROGRAM_H
#define APP_PROGRAM_H

#include <stdint.h>

#define PROGRAM_BAUD 115200u

/* On a radio (2026-11-22, first try) starting the cable UART in normal mode blanked the screen and made the external
 * flash read as empty (CHIRP downloaded blank memories). The update mode does the same pin set-up and its screen
 * works, so the cause is not certain. PROGRAM_Init now checks the flash ID after the set-up and, if it is wrong,
 * finds and leaves off the clashing routing bit itself; it shows the result for 3 s at start-up (program.c). */
#ifndef PROGRAM_ENABLE
#define PROGRAM_ENABLE 1
#endif

enum { PROGRAM_IDLE, PROGRAM_DONE_READ, PROGRAM_DONE_WRITTEN };

/* Set up the cable UART at 115200 (call once at start-up). */
void PROGRAM_Init(void);

/* Call every main-loop tick. When the programming software says hello, this runs the whole session (blocking, with
 * the PROGRAM screen) and returns how it ended: PROGRAM_DONE_WRITTEN means the channels or settings changed and the
 * app must reload them. */
int  PROGRAM_Poll(void);

#endif
