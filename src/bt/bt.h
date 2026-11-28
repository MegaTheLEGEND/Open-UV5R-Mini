/* Open-UV5R-Mini: Bluetooth interface, the one header apps and the radio code use.
 *
 * Which backend is linked decides what works; BT_Caps() says which parts are there, so an app can show what it
 * can't do instead of failing:
 *
 *   bt/bt_none.c               the fully open build: no Bluetooth at all (BT_Caps() == 0)
 *   platform/jl_sdk/bt_spp.c   the Bluetooth edition (JieLi's SDK and its closed btstack/btctrler libraries, from
 *                              the user's own SDK copy): SPP serial only so far
 *
 * A bare-metal BLE backend on JieLi's controller library (btctrler.a speaks standard HCI) is the planned next step;
 * docs/BLUETOOTH.md "Bluetooth API and Flipper-style apps" has what it needs. Nothing else in the firmware depends
 * on which backend is linked.
 *
 * All calls are from the main loop (the apps' key and tick), never from an interrupt. Received data and scan reports
 * are polled, so an app never runs inside the Bluetooth code. */
#ifndef BT_BT_H
#define BT_BT_H

#include <stdint.h>
/* Inside the JieLi SDK, typedef.h already defines bool (as u8), true and false. */
#ifndef OPEN_UV5R_SDK_TYPES
#include <stdbool.h>
#endif

/* ------------------------------------------------------------------ what the linked backend can do */
#define BT_CAP_SPP      0x01u            /* classic serial port (a PC or phone sees a COM port) */
#define BT_CAP_LE_SCAN  0x02u            /* BLE scanning: advertising reports from devices nearby */
#define BT_CAP_LE_ADV   0x04u            /* BLE advertising: the radio's own beacon */
#define BT_CAP_LE_DATA  0x08u            /* BLE data link to a phone (one serial-like service) */
#define BT_CAP_HID      0x10u            /* the radio as a BLE keyboard / remote */

uint8_t     BT_Caps(void);
const char *BT_Backend(void);            /* "NONE", "JL SDK", ...: up to 12 characters */
bool        BT_GetAddress(uint8_t addr[6]); /* the radio's own address, most significant byte first; false: unknown */

/* ------------------------------------------------------------------ serial link (SPP or the BLE data service) */
typedef void (*BT_ReceiveHandler_t)(const uint8_t *data, uint16_t len);

bool BT_IsAvailable(void);               /* a serial link exists in this build (BT_CAP_SPP or BT_CAP_LE_DATA) */
void BT_Init(BT_ReceiveHandler_t on_receive);
bool BT_IsConnected(void);
int  BT_Send(const uint8_t *data, uint16_t len); /* 0 on success */

/* Pairing (BLE: bt/smp.h). Pairing mode accepts a new phone or PC; a paired one reconnects encrypted any time. */
typedef enum { BT_PAIR_NONE, BT_PAIR_PAIRING, BT_PAIR_PAIRED, BT_PAIR_FAILED } BT_PairState_t;
void           BT_SetPairable(bool on);  /* pairing mode on or off */
bool           BT_Pairable(void);
BT_PairState_t BT_PairState(void);       /* of the link now: PAIRED = encrypted with a paired phone's key */
int32_t        BT_Passkey(void);         /* the six digits to show while a phone pairs by passkey, or -1 */

/* ------------------------------------------------------------------ BLE scanning (BT_CAP_LE_SCAN) */
#define BT_ADV_MAX 31                    /* legacy advertising: up to 31 bytes of AD structures */

typedef enum {                           /* the HCI LE Advertising Report's event types */
	BT_ADV_IND = 0,                      /* connectable, scannable */
	BT_ADV_DIRECT_IND = 1,
	BT_ADV_SCAN_IND = 2,                 /* scannable, not connectable */
	BT_ADV_NONCONN_IND = 3,              /* a beacon */
	BT_ADV_SCAN_RSP = 4,                 /* the answer to our scan request (active scanning) */
} BT_AdvType_t;

typedef struct {
	uint8_t addr[6];                     /* most significant byte first, as printed: AA:BB:CC:DD:EE:FF */
	uint8_t addr_type;                   /* 0 public, 1 random */
	uint8_t type;                        /* BT_AdvType_t */
	int8_t  rssi;                        /* dBm */
	uint8_t len;                         /* bytes in data */
	uint8_t data[BT_ADV_MAX];            /* AD structures (bt/le_adv.h parses them) */
} BT_LeReport_t;

bool BT_LeScanStart(bool active);        /* active: also ask for scan responses (more names); false: can't */
void BT_LeScanStop(void);
bool BT_LePoll(BT_LeReport_t *out);      /* the next report heard, if any */

/* ------------------------------------------------------------------ BLE advertising (BT_CAP_LE_ADV) */
/* The radio's own advertising: data is AD structures (up to 31 bytes), interval 20..10240 ms, non-connectable. */
bool BT_LeAdvStart(const uint8_t *data, uint8_t len, uint16_t interval_ms);
void BT_LeAdvStop(void);

#endif
