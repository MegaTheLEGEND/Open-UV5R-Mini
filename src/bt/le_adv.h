/* Open-UV5R-Mini: BLE advertising data, read (bt/le_adv.c). Pure C, no hardware: tests/btle_host.py runs it on the PC.
 *
 * Advertising data is a list of AD structures: [length][type][length-1 bytes] (Bluetooth Core Supplement, part A).
 * LeAdv_Parse picks out what a scanner list shows: the name, flags, transmit power, the first 16-bit service UUID
 * and the manufacturer's company ID, and a guess at what kind of device it is. LeList keeps the devices heard. */
#ifndef BT_LE_ADV_H
#define BT_LE_ADV_H

#include <stdbool.h>
#include <stdint.h>
#include "bt/bt.h"

#define LE_NAME_MAX 12                   /* characters kept of the name (the screen row has room for that) */

typedef enum {
	LE_KIND_OTHER = 0,
	LE_KIND_IBEACON,                     /* Apple company ID, type 0x02 length 0x15 */
	LE_KIND_EDDYSTONE,                   /* service 0xFEAA */
	LE_KIND_APPLE,                       /* other Apple data (phones, watches, AirPods, Find My) */
	LE_KIND_MICROSOFT,
	LE_KIND_GOOGLE,                      /* Fast Pair (0xFE2C) and Google's company ID */
	LE_KIND_SAMSUNG,
	LE_KIND_TILE,                        /* service 0xFEED / 0xFEEC */
	LE_KIND_HID,                         /* advertises the HID service (0x1812): keyboard, mouse, remote */
	LE_KIND_HEART,                       /* heart rate (0x180D) */
	LE_KIND_COUNT
} LeKind_t;

typedef struct {
	char     name[LE_NAME_MAX + 1];      /* "" if none; only printable ASCII, others as '?' */
	bool     name_full;                  /* complete name (0x09), not shortened (0x08) */
	uint8_t  flags;                      /* the Flags structure (0x01), 0 if none */
	bool     has_tx;
	int8_t   tx_dbm;                     /* 0x0A */
	uint16_t uuid16;                     /* the first 16-bit service UUID (0x02/0x03/0x16), 0 if none */
	bool     has_company;
	uint16_t company;                    /* manufacturer data (0xFF): the first two bytes, little-endian */
	uint8_t  kind;                       /* LeKind_t */
	bool     bad;                        /* a structure ran past the end: the rest was ignored */
} LeAdvInfo_t;

/* Fills *out from len bytes of AD structures. Returns false if the data was malformed (out still has what was read). */
bool        LeAdv_Parse(const uint8_t *d, uint8_t len, LeAdvInfo_t *out);
const char *LeAdv_KindName(uint8_t kind);       /* "IBEACON", "APPLE", ... ("" for LE_KIND_OTHER) */
const char *LeAdv_CompanyName(uint16_t id);     /* a few well-known ones, else 0 */
/* "AA:BB:CC:DD:EE:FF" into b (18 bytes); returns the end */
char       *LeAdv_FormatAddr(char *b, const uint8_t addr[6]);

/* ------------------------------------------------------------------ the devices heard */
#define LE_LIST_MAX 16

typedef struct {
	uint8_t     addr[6];
	uint8_t     addr_type;
	uint8_t     type;                    /* BT_AdvType_t of the last advertisement (not scan responses) */
	int8_t      rssi;                    /* the last one */
	int8_t      rssi_max;
	uint16_t    count;                   /* reports heard, up to 65535 */
	uint32_t    first_ms, last_ms;
	LeAdvInfo_t info;                    /* merged: a scan response's name fills in a missing one */
	uint8_t     len;                     /* the last advertisement's raw data (not the scan response) */
	uint8_t     data[BT_ADV_MAX];
} LeDevice_t;

typedef struct {
	LeDevice_t dev[LE_LIST_MAX];
	uint8_t    n;
	uint32_t   reports;                  /* all reports added */
	uint32_t   dropped;                  /* new devices not kept: the list was full of fresher ones */
} LeList_t;

void LeList_Clear(LeList_t *l);
/* Adds a report heard at now_ms; returns the device's index. A full list replaces the one heard longest ago. */
int  LeList_Add(LeList_t *l, const BT_LeReport_t *r, uint32_t now_ms);
/* Drops devices not heard for max_age_ms. */
void LeList_Age(LeList_t *l, uint32_t now_ms, uint32_t max_age_ms);
/* Indexes into l->dev, strongest last RSSI first (ties: the most recently heard first). Returns how many. */
int  LeList_Sorted(const LeList_t *l, uint8_t order[LE_LIST_MAX]);

/* ------------------------------------------------------------------ the radio's own beacon (BT_LeAdvStart) */
/* Each writes AD structures into out (BT_ADV_MAX bytes) and returns the length, or 0 if it doesn't fit.
 * All start with Flags = 0x06 (LE general discoverable, no BR/EDR). */
uint8_t LeAdv_BuildName(uint8_t *out, const char *name);                 /* the complete local name */
/* Eddystone-URL (service 0xFEAA, frame 0x10): "https://" / "http://" (with or without "www.") and ".com/", ".org/",
 * ... are shortened as the format says; tx_dbm is the calibrated power at 0 m. */
uint8_t LeAdv_BuildEddystoneUrl(uint8_t *out, const char *url, int8_t tx_dbm);
/* iBeacon: Apple's company ID, type 0x02 0x15, 16-byte UUID, major, minor, measured power at 1 m */
uint8_t LeAdv_BuildIBeacon(uint8_t *out, const uint8_t uuid[16], uint16_t major, uint16_t minor, int8_t rssi_1m);

#endif
