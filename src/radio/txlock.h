/* Open-UV5R-Mini: where transmit is allowed (the TX lock), chosen on the start-up config page (app/config.c).
 *
 * Modes: LOCKED (no transmit), HAM (the amateur bands), LOCAL (the region's personal radio service: GMRS in the US,
 * PMR446 in Europe, UHF CB in Australia), HAM+LOCAL, UNLOCKED (anywhere the radio has a transmit path). The default is
 * HAM (AGENTS.md rule 5). Whatever the mode, transmit also needs a transmit path (radio.c) and calibration.
 *
 * Build switches (features.mk):
 *   TXLOCK_REGIONS 0  nothing region specific in the image: only LOCKED, HAM on the bands common to all three ITU
 *                     regions (144-146, 430-440 MHz) and UNLOCKED.
 *   TXLOCK_REGION  n  the image is locked to region n (1 US, 2 EU/UK, 3 AU); 0 = chosen on the config page.
 *   TXLOCK_UNLOCK  0  no UNLOCKED mode in the image.
 *
 * The local services have their own rules (licence, power, type approval, fixed antenna). The radio checks the
 * channel, bandwidth and power limit below; the rest is the user's responsibility. This radio is not type approved
 * for any of them.
 */
#ifndef RADIO_TXLOCK_H
#define RADIO_TXLOCK_H

#include <stdbool.h>
#include <stdint.h>

#ifndef TXLOCK_REGIONS
#define TXLOCK_REGIONS 1
#endif
#ifndef TXLOCK_REGION
#define TXLOCK_REGION 0
#endif
#ifndef TXLOCK_UNLOCK
#define TXLOCK_UNLOCK 1
#endif
#if !TXLOCK_REGIONS && TXLOCK_REGION
#error "TXLOCK_REGION needs TXLOCK_REGIONS=1"
#endif

typedef enum { TXL_LOCKED, TXL_HAM, TXL_LOCAL, TXL_HAM_LOCAL, TXL_UNLOCKED, TXL_COUNT } TxLock_t;
typedef enum { REGION_ITU, REGION_US, REGION_EU, REGION_AU, REGION_COUNT } Region_t;

#define REGION_DEFAULT REGION_US   /* when the region is chosen on the config page and none was saved */

void        TXLOCK_Set(TxLock_t mode, Region_t region);   /* out-of-range or unavailable values fall back */
TxLock_t    TXLOCK_Mode(void);
Region_t    TXLOCK_Region(void);
bool        TXLOCK_RegionFixed(void);                     /* the build decides the region */
bool        TXLOCK_Available(TxLock_t mode, Region_t region);
const char *TXLOCK_ModeName(TxLock_t mode, Region_t region);
const char *TXLOCK_RegionName(Region_t region);
/* Why transmit on f (10 Hz) is not allowed in the current mode, or 0 if it is. */
const char *TXLOCK_Check(uint32_t f, bool low_power, bool narrow);

#endif
