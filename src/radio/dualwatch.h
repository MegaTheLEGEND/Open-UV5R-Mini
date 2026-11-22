/* Open-UV5R-Mini: dual watch timing (stock setting 0x04). app/app.c does the tuning; this decides when.
 *
 * Active side quiet for 300 ms: listen on the other side for 100 ms (long enough for our squelch to open, 30 ms,
 * after the RSSI settles). If it opens there, stay and show it until it has been quiet for 1 s. The timing is our
 * own choice; the stock app's is not traced yet.
 */
#ifndef APP_DUALWATCH_H
#define APP_DUALWATCH_H

#include <stdbool.h>
#include <stdint.h>

#define DW_ACTIVE_MS 300u
#define DW_PEEK_MS   100u
#define DW_HANG_MS   1000u

typedef enum { DW_NONE = 0, DW_BEGIN, DW_END, DW_HELD } DwAction_t;

void       DW_Reset(void);                 /* back on the active side (after dual watch was ended) */
bool       DW_Peeking(void);
bool       DW_Held(void);                  /* peeking and the other side opened */
/* Every tick. busy: something needs the active side (PTT, scan, entry, menu). open: squelch open now. */
DwAction_t DW_Tick(bool enabled, bool busy, bool open, uint16_t tick_ms);

#endif
