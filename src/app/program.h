/* Open-UV5R-Mini: programming over the cable in normal mode, as CHIRP's UV-5R Mini driver expects. See program.c. */
#ifndef APP_PROGRAM_H
#define APP_PROGRAM_H

#include <stdint.h>

#define PROGRAM_BAUD 115200u

enum { PROGRAM_IDLE, PROGRAM_DONE_READ, PROGRAM_DONE_WRITTEN };

/* Set up the cable UART at 115200 (call once at start-up). */
void PROGRAM_Init(void);

/* Call every main-loop tick. When the programming software says hello, this runs the whole session (blocking, with
 * the PROGRAM screen) and returns how it ended: PROGRAM_DONE_WRITTEN means the channels or settings changed and the
 * app must reload them. */
int  PROGRAM_Poll(void);

#endif
