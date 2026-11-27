/* Open-UV5R-Mini: a tiny BLE GATT server with the Nordic UART Service (bt/gatt.h). Our own code, written from the
 * Bluetooth Core Specification (ATT, GATT, L2CAP and SMP for LE) and Nordic's published NUS UUIDs. */
#include "bt/gatt.h"

/* ATT opcodes */
#define ATT_ERROR_RSP        0x01
#define ATT_MTU_REQ          0x02
#define ATT_MTU_RSP          0x03
#define ATT_FIND_INFO_REQ    0x04
#define ATT_FIND_INFO_RSP    0x05
#define ATT_FIND_TYPE_REQ    0x06
#define ATT_FIND_TYPE_RSP    0x07
#define ATT_READ_TYPE_REQ    0x08
#define ATT_READ_TYPE_RSP    0x09
#define ATT_READ_REQ         0x0A
#define ATT_READ_RSP         0x0B
#define ATT_READ_BLOB_REQ    0x0C
#define ATT_READ_BLOB_RSP    0x0D
#define ATT_READ_GROUP_REQ   0x10
#define ATT_READ_GROUP_RSP   0x11
#define ATT_WRITE_REQ        0x12
#define ATT_WRITE_RSP        0x13
#define ATT_NOTIFY           0x1B
#define ATT_WRITE_CMD        0x52

/* ATT errors */
#define E_INVALID_HANDLE     0x01
#define E_READ_NOT_PERM      0x02
#define E_WRITE_NOT_PERM     0x03
#define E_INVALID_PDU        0x04
#define E_NOT_SUPPORTED      0x06
#define E_INVALID_OFFSET     0x07
#define E_NOT_FOUND          0x0A
#define E_UNSUPPORTED_GROUP  0x10

/* GATT UUIDs */
#define U_PRIMARY   0x2800
#define U_CHAR      0x2803
#define U_CCCD      0x2902
#define U_NAME      0x2A00
#define U_APPEAR    0x2A01

/* Nordic UART Service UUIDs, little-endian as on the air: 6E40000x-B5A3-F393-E0A9-E50E24DCCA9E */
#define NUS(x) { 0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, (x), 0x00, 0x40, 0x6E }
static const uint8_t NUS_SVC[16] = NUS(0x01), NUS_RX[16] = NUS(0x02), NUS_TX[16] = NUS(0x03);

#define P_READ  1
#define P_WRITE 2

typedef struct {
	uint16_t       type;                 /* 16-bit attribute type (UUID) */
	const uint8_t *type128;              /* or a 128-bit one (then type = 0) */
	const uint8_t *val;                  /* fixed value, or 0 for the ones made at run time */
	uint8_t        len;
	uint8_t        perm;
} Attr_t;

static const uint8_t V_GAP[2] = { 0x00, 0x18 };
static const uint8_t V_NAME_DECL[5] = { 0x02, GATT_H_NAME, 0x00, 0x00, 0x2A };
static const uint8_t V_APP_DECL[5] = { 0x02, GATT_H_APP, 0x00, 0x01, 0x2A };
static const uint8_t V_APP[2] = { 0x00, 0x00 };
static const uint8_t V_RX_DECL[19] = { 0x0C, GATT_H_RX, 0x00, 0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93,
                                       0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E };
static const uint8_t V_TX_DECL[19] = { 0x10, GATT_H_TX, 0x00, 0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93,
                                       0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E };

static const Attr_t DB[GATT_H_LAST + 1] = {
	[GATT_H_GAP]       = { U_PRIMARY, 0, V_GAP, 2, P_READ },
	[GATT_H_NAME_DECL] = { U_CHAR, 0, V_NAME_DECL, 5, P_READ },
	[GATT_H_NAME]      = { U_NAME, 0, 0, 0, P_READ },
	[GATT_H_APP_DECL]  = { U_CHAR, 0, V_APP_DECL, 5, P_READ },
	[GATT_H_APP]       = { U_APPEAR, 0, V_APP, 2, P_READ },
	[GATT_H_NUS]       = { U_PRIMARY, 0, NUS_SVC, 16, P_READ },
	[GATT_H_RX_DECL]   = { U_CHAR, 0, V_RX_DECL, 19, P_READ },
	[GATT_H_RX]        = { 0, NUS_RX, 0, 0, P_WRITE },
	[GATT_H_TX_DECL]   = { U_CHAR, 0, V_TX_DECL, 19, P_READ },
	[GATT_H_TX]        = { 0, NUS_TX, 0, 0, 0 },
	[GATT_H_TX_CCCD]   = { U_CCCD, 0, 0, 2, P_READ | P_WRITE },
};

static struct {
	char             name[21];
	uint8_t          name_len;
	uint8_t          cccd[2];
	GATT_RxHandler_t on_rx;
} G;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void     wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

void GATT_Reset(const char *name, GATT_RxHandler_t on_rx)
{
	uint8_t n = 0;
	while (name && name[n] && n < 20u) {
		G.name[n] = name[n];
		n++;
	}
	G.name[n] = 0;
	G.name_len = n;
	G.cccd[0] = G.cccd[1] = 0;
	G.on_rx = on_rx;
}

bool GATT_NotifyOn(void) { return (G.cccd[0] & 1u) != 0; }

/* the value of handle h right now */
static const uint8_t *value(uint16_t h, uint8_t *len)
{
	if (h == GATT_H_NAME) {
		*len = G.name_len;
		return (const uint8_t *)G.name;
	}
	if (h == GATT_H_TX_CCCD) {
		*len = 2;
		return G.cccd;
	}
	*len = DB[h].len;
	return DB[h].val;
}

static int error(uint8_t *o, uint8_t req, uint16_t h, uint8_t code)
{
	o[0] = ATT_ERROR_RSP;
	o[1] = req;
	wr16(o + 2, h);
	o[4] = code;
	return 5;
}

/* the last handle of the service that starts at h */
static uint16_t group_end(uint16_t h)
{
	uint16_t e = h;
	while (e + 1u <= GATT_H_LAST && DB[e + 1u].type != U_PRIMARY)
		e++;
	return e;
}

/* is the attribute's type the UUID in u (2 or 16 bytes)? */
static bool type_is(const Attr_t *a, const uint8_t *u, int ulen)
{
	if (ulen == 2)
		return a->type && a->type == rd16(u);
	if (ulen == 16) {
		if (!a->type128)
			return false;
		for (int i = 0; i < 16; i++)
			if (a->type128[i] != u[i])
				return false;
		return true;
	}
	return false;
}

static bool range(const uint8_t *in, int len, uint16_t *s, uint16_t *e)
{
	if (len < 5)
		return false;
	*s = rd16(in + 1);
	*e = rd16(in + 3);
	return *s && *s <= *e;
}

static int att(const uint8_t *in, int len, uint8_t *o)
{
	uint8_t op = in[0], vlen;
	uint16_t s, e, h;
	const uint8_t *v;
	int n;

	switch (op) {
	case ATT_MTU_REQ:
		if (len != 3)
			return error(o, op, 0, E_INVALID_PDU);
		o[0] = ATT_MTU_RSP;
		wr16(o + 1, GATT_MTU);       /* we stay at 23 */
		return 3;

	case ATT_FIND_INFO_REQ:
		if (!range(in, len, &s, &e))
			return error(o, op, len >= 3 ? rd16(in + 1) : 0, E_INVALID_HANDLE);
		n = 2;
		o[0] = ATT_FIND_INFO_RSP;
		o[1] = 0;
		for (h = s; h <= e && h <= GATT_H_LAST; h++) {
			uint8_t fmt = DB[h].type ? 1 : 2, need = fmt == 1 ? 4 : 18;
			if (o[1] && o[1] != fmt)
				break;               /* one format per answer */
			if (n + need > GATT_MTU)
				break;
			o[1] = fmt;
			wr16(o + n, h);
			if (fmt == 1)
				wr16(o + n + 2, DB[h].type);
			else
				for (int i = 0; i < 16; i++)
					o[n + 2 + i] = DB[h].type128[i];
			n += need;
		}
		return n > 2 ? n : error(o, op, s, E_NOT_FOUND);

	case ATT_FIND_TYPE_REQ:              /* only primary services by their UUID */
		if (!range(in, len, &s, &e) || len < 7)
			return error(o, op, 0, E_INVALID_HANDLE);
		n = 1;
		o[0] = ATT_FIND_TYPE_RSP;
		for (h = s; h <= e && h <= GATT_H_LAST; h++) {
			if (DB[h].type != rd16(in + 5) || rd16(in + 5) != U_PRIMARY)
				continue;
			v = value(h, &vlen);
			if (vlen != len - 7)
				continue;
			bool same = true;
			for (int i = 0; i < vlen; i++)
				if (v[i] != in[7 + i])
					same = false;
			if (!same || n + 4 > GATT_MTU)
				continue;
			wr16(o + n, h);
			wr16(o + n + 2, group_end(h));
			n += 4;
		}
		return n > 1 ? n : error(o, op, s, E_NOT_FOUND);

	case ATT_READ_GROUP_REQ:
		if (!range(in, len, &s, &e) || (len != 7 && len != 21))
			return error(o, op, 0, E_INVALID_HANDLE);
		if (len != 7 || rd16(in + 5) != U_PRIMARY)
			return error(o, op, s, E_UNSUPPORTED_GROUP);
		n = 2;
		o[0] = ATT_READ_GROUP_RSP;
		o[1] = 0;
		for (h = s; h <= e && h <= GATT_H_LAST; h++) {
			if (DB[h].type != U_PRIMARY)
				continue;
			v = value(h, &vlen);
			if (o[1] && o[1] != 4 + vlen)
				break;               /* one length per answer */
			if (n + 4 + vlen > GATT_MTU)
				break;
			o[1] = (uint8_t)(4 + vlen);
			wr16(o + n, h);
			wr16(o + n + 2, group_end(h));
			for (int i = 0; i < vlen; i++)
				o[n + 4 + i] = v[i];
			n += 4 + vlen;
		}
		return n > 2 ? n : error(o, op, s, E_NOT_FOUND);

	case ATT_READ_TYPE_REQ:
		if (!range(in, len, &s, &e) || (len != 7 && len != 21))
			return error(o, op, 0, E_INVALID_HANDLE);
		n = 2;
		o[0] = ATT_READ_TYPE_RSP;
		o[1] = 0;
		for (h = s; h <= e && h <= GATT_H_LAST; h++) {
			if (!type_is(&DB[h], in + 5, len - 5))
				continue;
			if (!(DB[h].perm & P_READ)) {
				if (n == 2)
					return error(o, op, h, E_READ_NOT_PERM);
				break;
			}
			v = value(h, &vlen);
			if (vlen > GATT_MTU - 4)
				vlen = GATT_MTU - 4;
			if (o[1] && o[1] != 2 + vlen)
				break;
			if (n + 2 + vlen > GATT_MTU)
				break;
			o[1] = (uint8_t)(2 + vlen);
			wr16(o + n, h);
			for (int i = 0; i < vlen; i++)
				o[n + 2 + i] = v[i];
			n += 2 + vlen;
		}
		return n > 2 ? n : error(o, op, s, E_NOT_FOUND);

	case ATT_READ_REQ:
	case ATT_READ_BLOB_REQ: {
		uint16_t off = 0;
		if (len < 3 || (op == ATT_READ_BLOB_REQ && len < 5))
			return error(o, op, 0, E_INVALID_PDU);
		h = rd16(in + 1);
		if (!h || h > GATT_H_LAST)
			return error(o, op, h, E_INVALID_HANDLE);
		if (!(DB[h].perm & P_READ))
			return error(o, op, h, E_READ_NOT_PERM);
		v = value(h, &vlen);
		if (op == ATT_READ_BLOB_REQ) {
			off = rd16(in + 3);
			if (off > vlen)
				return error(o, op, h, E_INVALID_OFFSET);
		}
		o[0] = op == ATT_READ_REQ ? ATT_READ_RSP : ATT_READ_BLOB_RSP;
		n = 1;
		for (int i = off; i < vlen && n < GATT_MTU; i++)
			o[n++] = v[i];
		return n;
	}

	case ATT_WRITE_REQ:
	case ATT_WRITE_CMD:
		if (len < 3)
			return op == ATT_WRITE_CMD ? 0 : error(o, op, 0, E_INVALID_PDU);
		h = rd16(in + 1);
		if (!h || h > GATT_H_LAST)
			return op == ATT_WRITE_CMD ? 0 : error(o, op, h, E_INVALID_HANDLE);
		if (!(DB[h].perm & P_WRITE))
			return op == ATT_WRITE_CMD ? 0 : error(o, op, h, E_WRITE_NOT_PERM);
		if (h == GATT_H_TX_CCCD) {
			if (len != 5)
				return op == ATT_WRITE_CMD ? 0 : error(o, op, h, 0x0D);   /* invalid value length */
			G.cccd[0] = in[3];
			G.cccd[1] = in[4];
		} else if (h == GATT_H_RX && G.on_rx && len > 3)
			G.on_rx(in + 3, (uint16_t)(len - 3));
		if (op == ATT_WRITE_CMD)
			return 0;
		o[0] = ATT_WRITE_RSP;
		return 1;

	default:
		if (op & 0x40)                   /* a command: never answered */
			return 0;
		if (op == ATT_ERROR_RSP || op == 0x1E || (op & 1u))
			return 0;                    /* responses and confirmations: nothing to answer */
		return error(o, op, 0, E_NOT_SUPPORTED);
	}
}

int GATT_Input(uint16_t cid, const uint8_t *in, int len, uint8_t *out, uint16_t *out_cid)
{
	*out_cid = cid;
	if (len < 1)
		return 0;
	if (cid == L2CAP_CID_ATT) {
		if (len > GATT_MTU)
			len = GATT_MTU;
		return att(in, len, out);
	}
	if (cid == L2CAP_CID_SMP) {          /* any pairing request: Pairing Failed, "pairing not supported" */
		if (in[0] == 0x01 || in[0] == 0x0B) {
			out[0] = 0x05;
			out[1] = 0x05;
			return 2;
		}
		return 0;
	}
	if (cid == L2CAP_CID_SIG && len >= 4) {  /* requests (even codes from the central): command not understood */
		uint8_t code = in[0];
		if (code == 0x01 || (code & 1u))
			return 0;                    /* a reject, or a response */
		out[0] = 0x01;                   /* Command Reject */
		out[1] = in[1];                  /* the same identifier */
		wr16(out + 2, 2);
		wr16(out + 4, 0x0000);           /* command not understood */
		return 6;
	}
	return 0;
}

int GATT_Notify(const uint8_t *data, int len, uint8_t *out)
{
	if (len > GATT_CHUNK)
		len = GATT_CHUNK;
	out[0] = ATT_NOTIFY;
	wr16(out + 1, GATT_H_TX);
	for (int i = 0; i < len; i++)
		out[3 + i] = data[i];
	return 3 + len;
}
