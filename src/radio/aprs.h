/* Open-UV5R-Mini: APRS on top of radio/ax25.c (Sean asked, 2026-11-27). From the public APRS 1.01 specification.
 *
 *   Sending   a position report "!DDMM.mmN/DDDMM.mmW>comment" from our own call sign, to APRS_TOCALL, through
 *             APRS_PATH. The settings fit in 10 bytes (APRS_Pack) kept with the radio's settings.
 *   Receiving what a heard packet is, for the list on the screen: who, what kind, its position (plain, compressed
 *             or Mic-E) and how far away it is from ours, or the text of a message / status.
 * No radio code in here: tested on the PC (tests/aprs_host.py).
 */
#ifndef RADIO_APRS_H
#define RADIO_APRS_H

#include <stdbool.h>
#include <stdint.h>

#define APRS_TOCALL "APZUV5"             /* APZ: experimental software (APRS tocall list) */
#define APRS_PATH   "WIDE1-1,WIDE2-1"
#define APRS_COMMENT "Open-UV5R-Mini"
#define APRS_ICONS  8u

/* latitude and longitude in 1/100 minute: + north / east */
#define APRS_LAT_MAX (90 * 6000)
#define APRS_LON_MAX (180 * 6000)

typedef struct {
	char    call[7];                     /* "" = not set: no sending */
	uint8_t ssid;                        /* 0-15 */
	int32_t lat, lon;                    /* 1/100 minute (kept to 1/10 minute, about 185 m) */
	uint8_t icon;                        /* 0..APRS_ICONS-1 */
	uint8_t every;                       /* beacon: 0 off, 1-3 = every 10, 20, 30 minutes while the app is open */
} AprsConf_t;

#define APRS_PACK_LEN 10u
void        APRS_Pack(const AprsConf_t *c, uint8_t out[APRS_PACK_LEN]);
void        APRS_Unpack(const uint8_t in[APRS_PACK_LEN], AprsConf_t *c);
const char *APRS_IconName(uint8_t i);
/* "N0CALL-9" (no "-0") */
char       *APRS_CallText(char *out, const AprsConf_t *c);

/* The information field of our position report; returns its length */
unsigned APRS_PositionInfo(const AprsConf_t *c, char *out, unsigned max);
/* The whole AX.25 frame (with FCS) of our position report; 0 if the call sign isn't set */
unsigned APRS_BuildBeacon(const AprsConf_t *c, uint8_t *frame, unsigned max);

/* "4903.50N" / "07201.75W" from 1/100 minute; returns the end */
char *APRS_LatText(char *b, int32_t lat);
char *APRS_LonText(char *b, int32_t lon);

typedef struct {
	char    from[10];                    /* "N0CALL-9" */
	char    kind[7];                     /* "POS", "MIC-E", "MSG", "MSG ME", "STATUS", "OBJECT", "WX", ... */
	char    text[44];                    /* the comment, message or status (cut) */
	bool    has_pos;
	int32_t lat, lon;                    /* 1/100 minute */
	char    sym_table, sym;
} AprsHeard_t;

/* From a frame as AX25_ToText prints it ("FROM>TO,PATH:info"); me = our call sign text to spot messages to us
 * (may be ""). False if it isn't a frame at all. */
bool APRS_Describe(const char *tnc2, const char *me, AprsHeard_t *h);

/* Distance in tenths of a km (or of a mile) and bearing in degrees from (lat1, lon1) to (lat2, lon2) */
uint32_t APRS_Distance(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2, bool miles, uint16_t *bearing);

#endif
