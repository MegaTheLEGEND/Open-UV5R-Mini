/* Open-UV5R-Mini: pairing mode in the Bluetooth link apps (BT SERIAL, BT CHIRP), app/btpair.c.
 *
 * 5 switches pairing mode on for PAIR_SECONDS (or off again). While it is on, a phone or PC may pair: Windows'
 * "Add a device", a phone's Bluetooth settings, or a pairing asked for by the app. When it pairs by passkey, the
 * six digits show on the radio to type on the phone or PC. A paired phone reconnects encrypted later, without
 * pairing mode (bt/smp.h). */
#ifndef APP_BTPAIR_H
#define APP_BTPAIR_H

#include <stdbool.h>
#include "driver/keyboard.h"

#define PAIR_SECONDS 60u

void BTPAIR_Enter(void);                 /* the app opened: pairing mode off */
void BTPAIR_Leave(void);                 /* and closed */
bool BTPAIR_Key(KEY_Code_t k);           /* true: it was the pairing key */
/* every tick: the status line ("5 PAIR", "PAIRING 59S", "KEY 123456", "PAIRED", "PAIR FAILED") into line (22 bytes);
 * true when it changed since the last call */
bool BTPAIR_Tick(char *line, bool *big);

#endif
