/* Open-UV5R-Mini: a tiny BLE GATT server with one serial service (bt/gatt.c). Pure C, no hardware: tests/btle_host.py
 * runs it on the PC; platform/jl_sdk/bt_le.c feeds it the L2CAP frames of a BLE connection.
 *
 * The serial service is the Nordic UART Service (NUS), which phone apps (nRF Connect, Serial Bluetooth Terminal,
 * Bluefruit Connect) and web pages (Web Bluetooth) speak, and tools/ble_serial.py bridges to CHIRP:
 *   service 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
 *   RX      6E400002-...  the phone writes bytes to the radio (write, write without response)
 *   TX      6E400003-...  the radio sends bytes to the phone (notify; the phone switches it on with the CCCD)
 * plus the GAP service (device name, appearance). No pairing: Security Manager requests get "pairing not
 * supported", L2CAP signalling requests "command not understood". ATT MTU stays 23, so a notification carries up to
 * 20 bytes and every frame fits one 27-byte link-layer packet. */
#ifndef BT_GATT_H
#define BT_GATT_H

#include <stdbool.h>
#include <stdint.h>

#define GATT_MTU      23
#define GATT_CHUNK    (GATT_MTU - 3)     /* bytes in one notification */

#define L2CAP_CID_ATT 0x0004
#define L2CAP_CID_SIG 0x0005
#define L2CAP_CID_SMP 0x0006

typedef void (*GATT_RxHandler_t)(const uint8_t *data, uint16_t len);

/* A new connection: everything back to the start (notifications off). name: the GAP device name, up to 20 chars. */
void GATT_Reset(const char *name, GATT_RxHandler_t on_rx);
/* One L2CAP payload received on channel cid. Writes the answer (if any) into out (GATT_MTU + 4 bytes at least) and
 * returns its length with its channel in *out_cid, or 0 for no answer. */
int  GATT_Input(uint16_t cid, const uint8_t *in, int len, uint8_t *out, uint16_t *out_cid);
/* The serial service needs an encrypted link (a paired phone): required (build switch BT_PAIRED_ONLY), encrypted
 * (the link now). While it is required and missing, writes and the CCCD answer "insufficient authentication", which
 * makes a phone pair (bt/smp.h). */
void GATT_Secure(bool required, bool encrypted);
/* The phone switched TX notifications on (it wrote 1 to the CCCD) */
bool GATT_NotifyOn(void);
/* One notification of TX with up to GATT_CHUNK bytes of data into out (ATT payload, for channel 4); returns its length. */
int  GATT_Notify(const uint8_t *data, int len, uint8_t *out);

/* the handles, for the tests */
enum {
	GATT_H_GAP = 1, GATT_H_NAME_DECL, GATT_H_NAME, GATT_H_APP_DECL, GATT_H_APP,
	GATT_H_NUS, GATT_H_RX_DECL, GATT_H_RX, GATT_H_TX_DECL, GATT_H_TX, GATT_H_TX_CCCD,
	GATT_H_LAST = GATT_H_TX_CCCD
};

#endif
