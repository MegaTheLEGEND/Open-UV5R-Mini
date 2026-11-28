/* Open-UV5R-Mini: BLE pairing, the Security Manager's peripheral side (bt/smp.c). Pure C, no hardware:
 * tests/btle_host.py runs it on the PC; platform/jl_sdk/bt_le.c feeds it channel 6 of a BLE connection and the
 * controller's encryption events.
 *
 * LE legacy pairing (Bluetooth Core, Vol 3 Part H), the radio as the responder:
 *   - Pairing is accepted only in **pairing mode** (SMP_SetPairable, switched on for a while by the apps), else the
 *     answer is "pairing not supported".
 *   - The radio has a display and no keyboard (IO capability DisplayOnly). A phone or PC with a keyboard that asks
 *     for protection (MITM) gets **passkey entry**: the radio shows six digits (SMP_Passkey), they are typed on the
 *     phone or PC. Others get Just Works (no number).
 *   - Bonding without storing anything: the long-term key we hand out is AES(ER, EDIV || Rand), where ER is a secret
 *     the radio makes from its own flash (the BTIF and calibration bytes; never written) and EDIV/Rand go to the
 *     phone with the key. When the phone comes back it sends EDIV/Rand and we make the same key again
 *     (SMP_LongTermKey), so a paired phone reconnects encrypted after a power cycle with no key store.
 *   - No LE Secure Connections (needs P-256 ECDH), no signing or identity keys.
 * Legacy pairing is weak against someone recording the pairing itself; passkey entry makes it harder, not safe. */
#ifndef BT_SMP_H
#define BT_SMP_H

#include <stdbool.h>
#include <stdint.h>

#define SMP_MAX 23                       /* the longest SMP packet we send */

typedef void (*SMP_Random_t)(uint8_t *out, int n);   /* random bytes (the platform's) */

/* A new connection. ia/ra: the phone's and our address as on the air (least significant byte first), iat/rat their
 * types (0 public, 1 random). er: the radio's 16-byte secret. */
void     SMP_Reset(const uint8_t ia[6], uint8_t iat, const uint8_t ra[6], uint8_t rat, const uint8_t er[16],
                   SMP_Random_t rnd);
/* Pairing mode on or off (accepting a new pairing) */
void     SMP_SetPairable(bool on);
bool     SMP_Pairable(void);
/* One packet received on channel 6: the answer (if any) into out, its length returned (0 = none) */
int      SMP_Input(const uint8_t *in, int len, uint8_t *out);
/* The controller asks for the key of this link (LE Long Term Key Request): rand and ediv as on the air. True and
 * the key (least significant byte first, as the HCI reply wants it); false = no key (negative reply). */
bool     SMP_LongTermKey(const uint8_t rand[8], uint16_t ediv, uint8_t ltk[16]);
/* The link is now encrypted (Encryption Change). Packets to send now (the keys we hand out) go into out1/out2;
 * returns how many (0-2), their lengths in len1/len2. */
int      SMP_Encrypted(uint8_t *out1, int *len1, uint8_t *out2, int *len2);
bool     SMP_IsEncrypted(void);

typedef enum { SMP_IDLE, SMP_PAIRING, SMP_PAIRED, SMP_FAILED } SmpState_t;
SmpState_t SMP_State(void);
/* the six digits to show while pairing by passkey, or -1 */
int32_t  SMP_Passkey(void);
uint8_t  SMP_FailReason(void);           /* the last Pairing Failed reason (sent or received) */

/* for the tests: AES-128 (FIPS-197, bytes in their usual order), and the pairing functions c1 and s1 on
 * little-endian values as the Bluetooth spec writes them */
void SMP_Aes(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);
void SMP_C1(const uint8_t k[16], const uint8_t r[16], const uint8_t preq[7], const uint8_t pres[7], uint8_t iat,
            const uint8_t ia[6], uint8_t rat, const uint8_t ra[6], uint8_t out[16]);
void SMP_S1(const uint8_t k[16], const uint8_t r1[16], const uint8_t r2[16], uint8_t out[16]);

#endif
