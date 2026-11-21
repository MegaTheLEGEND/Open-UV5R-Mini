/* Open-UV5R-Mini: speaker amplifier, LEDs and lamp (board.h). Pins are from the stock code, not yet confirmed on a
 * radio. */
#ifndef DRIVER_IO_H
#define DRIVER_IO_H

#include <stdbool.h>
#include <stdint.h>

/* Who wants the speaker: the amplifier is on while any of them does (the stock app keeps six such flags). */
enum {
	AUDIO_RX   = 1u << 0,
	AUDIO_BEEP = 1u << 1,
};

void IO_Init(void);
void IO_Amp(uint8_t who, bool on);
void IO_LedRx(bool on);
void IO_LedTx(bool on);
void IO_Lamp(bool on);
bool IO_LampIsOn(void);

#endif
