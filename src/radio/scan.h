/* Open-UV5R-Mini: scanning, with the stock app's three resume modes (stock setting byte 0x0A).
 *
 *   TIME     stop on a signal, move on after 5 s even if it is still there
 *   CARRIER  stop on a signal, move on 2 s after it goes
 *   SEARCH   stop on the first signal and end the scan
 *
 * The VFO scans the stock scan range (setting bytes 0x2C-0x2F, whole MHz) in the VFO's step; channel mode scans the
 * channels with the scan flag. This file holds only the timing; app/app.c does the hops. The dwell per hop is our
 * own choice (the receiver needs a moment for the RSSI to settle), not taken from the stock app.
 */
#ifndef APP_SCAN_H
#define APP_SCAN_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { SCAN_RESUME_TIME = 0, SCAN_RESUME_CARRIER = 1, SCAN_RESUME_SEARCH = 2 } ScanResume_t;
typedef enum { SCAN_IDLE = 0, SCAN_NONE, SCAN_HOP } ScanAction_t;

#define SCAN_DWELL_MS     60u
#define SCAN_TIME_HOLD_MS 5000u
#define SCAN_CARRIER_MS   2000u

void         SCAN_Start(int dir, ScanResume_t mode, uint16_t tick_ms);
void         SCAN_Stop(void);
bool         SCAN_Active(void);
bool         SCAN_Holding(void);          /* stopped on a signal */
int          SCAN_Dir(void);
/* Every tick, with the squelch state after the last hop: SCAN_HOP = move to the next frequency or channel now;
 * SCAN_IDLE = not scanning (also right after a SEARCH stop). */
ScanAction_t SCAN_Tick(bool open);

#endif
