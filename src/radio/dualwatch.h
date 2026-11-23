/* Open-UV5R-Mini: dual watch timing (stock setting 0x04). app/app.c does the tuning; this decides when.
 *
 * Active side without a signal for 300 ms: listen on the other side for 200 ms. The first 40 ms after the retune are
 * ignored (the RSSI and the chip's squelch still show the old side). A signal there holds the radio on that side
 * until it has been gone for 1 s; a signal on the active side keeps it there. "Signal" is a carrier
 * (RADIO_Carrier), not the full squelch: the tone detector needs a couple of hundred ms to lock, and swapping sides
 * restarts it (radio, 2026-11-22: with the full squelch and a 100 ms peek, tones never opened and the other side never
 * received). The timing is our own choice; the stock app's is not traced yet.
 */
#ifndef APP_DUALWATCH_H
#define APP_DUALWATCH_H

#include <stdbool.h>
#include <stdint.h>

#define DW_ACTIVE_MS 300u
#define DW_PEEK_MS   200u
#define DW_SETTLE_MS 40u
#define DW_HANG_MS   1000u

typedef enum { DW_NONE = 0, DW_BEGIN, DW_END, DW_HELD } DwAction_t;

void       DW_Reset(void);                 /* back on the active side (after dual watch was ended) */
bool       DW_Peeking(void);
bool       DW_Held(void);                  /* peeking and the other side has a signal */
/* Every tick. busy: something needs the active side (PTT, scan, entry, menu). open: a carrier now. */
DwAction_t DW_Tick(bool enabled, bool busy, bool open, uint16_t tick_ms);

#endif
