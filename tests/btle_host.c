/* Open-UV5R-Mini: PC test of src/bt/le_adv.c (tests/btle_host.py): BLE advertising data parsed, the device list. */
#include <stdio.h>
#include <string.h>
#include "bt/le_adv.h"
#include "bt/gatt.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static uint8_t rx_buf[64];
static int rx_len;
static void on_rx(const uint8_t *d, uint16_t n)
{
	memcpy(rx_buf + rx_len, d, n);
	rx_len += n;
}

static BT_LeReport_t rep(uint8_t last, int8_t rssi, uint8_t type, const uint8_t *d, uint8_t n)
{
	BT_LeReport_t r = { { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, last }, 1, type, rssi, n, { 0 } };
	memcpy(r.data, d, n);
	return r;
}

int main(void)
{
	LeAdvInfo_t in;
	char b[20];

	/* flags, complete name, TX power */
	{
		static const uint8_t d[] = { 2, 0x01, 0x06, 6, 0x09, 'U', 'V', '5', 'R', 'M', 2, 0x0A, 0xF4 };
		CHECK(LeAdv_Parse(d, sizeof d, &in));
		CHECK(in.flags == 0x06);
		CHECK(!strcmp(in.name, "UV5RM") && in.name_full);
		CHECK(in.has_tx && in.tx_dbm == -12);
		CHECK(in.kind == LE_KIND_OTHER && !in.has_company && !in.uuid16);
	}
	/* iBeacon: Apple manufacturer data, type 0x02 length 0x15 */
	{
		uint8_t d[30] = { 2, 0x01, 0x06, 26, 0xFF, 0x4C, 0x00, 0x02, 0x15 };
		CHECK(LeAdv_Parse(d, sizeof d, &in));
		CHECK(in.has_company && in.company == 0x004C);
		CHECK(in.kind == LE_KIND_IBEACON);
		CHECK(!strcmp(LeAdv_KindName(in.kind), "IBEACON"));
		CHECK(!strcmp(LeAdv_CompanyName(in.company), "APPLE"));
	}
	/* other Apple data */
	{
		static const uint8_t d[] = { 5, 0xFF, 0x4C, 0x00, 0x10, 0x05 };
		LeAdv_Parse(d, sizeof d, &in);
		CHECK(in.kind == LE_KIND_APPLE);
	}
	/* Eddystone: service UUID list 0xFEAA, then its service data */
	{
		static const uint8_t d[] = { 3, 0x03, 0xAA, 0xFE, 5, 0x16, 0xAA, 0xFE, 0x10, 0x00 };
		LeAdv_Parse(d, sizeof d, &in);
		CHECK(in.uuid16 == 0xFEAA && in.kind == LE_KIND_EDDYSTONE);
	}
	/* HID service; a shortened name, then a complete one wins; a long name is cut to LE_NAME_MAX */
	{
		static const uint8_t d[] = { 3, 0x02, 0x12, 0x18, 3, 0x08, 'K', 'B',
		                             15, 0x09, 'K', 'e', 'y', 'b', 'o', 'a', 'r', 'd', ' ', 'L', 'o', 'n', 'g', 1 };
		LeAdv_Parse(d, sizeof d, &in);
		CHECK(in.kind == LE_KIND_HID);
		CHECK(in.name_full && !strcmp(in.name, "Keyboard Lon"));
	}
	/* malformed: a structure runs past the end; zero length ends the data */
	{
		static const uint8_t d[] = { 2, 0x01, 0x05, 9, 0x09, 'A' };
		CHECK(!LeAdv_Parse(d, sizeof d, &in) && in.bad && in.flags == 0x05 && !in.name[0]);
		static const uint8_t z[] = { 2, 0x01, 0x04, 0, 0xFF, 0xFF };
		CHECK(LeAdv_Parse(z, sizeof z, &in) && in.flags == 0x04 && !in.has_company);
		CHECK(LeAdv_Parse(z, 0, &in) && !in.flags);
	}
	/* unprintable name bytes */
	{
		static const uint8_t d[] = { 4, 0x09, 'A', 0x01, 'B' };
		LeAdv_Parse(d, sizeof d, &in);
		CHECK(!strcmp(in.name, "A?B"));
	}
	/* address format */
	{
		static const uint8_t a[6] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB };
		CHECK(LeAdv_FormatAddr(b, a) == b + 17 && !strcmp(b, "01:23:45:67:89:AB"));
	}

	/* the list */
	{
		static LeList_t l;
		uint8_t ord[LE_LIST_MAX];
		static const uint8_t adv[] = { 2, 0x01, 0x06, 3, 0x03, 0x0D, 0x18 };
		static const uint8_t rsp[] = { 6, 0x09, 'S', 't', 'r', 'a', 'p' };
		static const uint8_t adv2[] = { 2, 0x01, 0x06 };
		BT_LeReport_t r;

		LeList_Clear(&l);
		r = rep(1, -70, BT_ADV_IND, adv, sizeof adv);
		CHECK(LeList_Add(&l, &r, 100) == 0);
		r = rep(1, -60, BT_ADV_SCAN_RSP, rsp, sizeof rsp);
		CHECK(LeList_Add(&l, &r, 110) == 0);
		CHECK(l.n == 1 && l.dev[0].count == 2 && l.dev[0].rssi == -60 && l.dev[0].rssi_max == -60);
		CHECK(!strcmp(l.dev[0].info.name, "Strap") && l.dev[0].info.kind == LE_KIND_HEART);
		CHECK(l.dev[0].type == BT_ADV_IND && l.dev[0].len == sizeof adv);   /* raw data: the advertisement */
		r = rep(1, -75, BT_ADV_IND, adv2, sizeof adv2);                      /* no name now: the old one stays */
		LeList_Add(&l, &r, 120);
		CHECK(!strcmp(l.dev[0].info.name, "Strap") && l.dev[0].rssi_max == -60 && l.dev[0].rssi == -75);

		r = rep(2, -40, BT_ADV_NONCONN_IND, adv2, sizeof adv2);
		CHECK(LeList_Add(&l, &r, 130) == 1);
		r = rep(3, -90, BT_ADV_NONCONN_IND, adv2, sizeof adv2);
		r.addr_type = 0;
		CHECK(LeList_Add(&l, &r, 140) == 2);
		r.addr_type = 1;                 /* same bytes, other address type: another device */
		CHECK(LeList_Add(&l, &r, 150) == 3);
		CHECK(l.n == 4 && l.reports == 6);
		CHECK(LeList_Sorted(&l, ord) == 4);
		CHECK(ord[0] == 1 && ord[1] == 0);
		CHECK(ord[2] == 3 && ord[3] == 2);   /* equal RSSI: the newer first */

		LeList_Age(&l, 1130, 1000);      /* heard at 130 and later stay */
		CHECK(l.n == 3 && l.dev[0].addr[5] == 2);

		LeList_Clear(&l);
		for (int i = 0; i < LE_LIST_MAX + 3; i++) {
			r = rep((uint8_t)(10 + i), -50, BT_ADV_IND, adv2, sizeof adv2);
			LeList_Add(&l, &r, (uint32_t)(1000 + i));
		}
		CHECK(l.n == LE_LIST_MAX && l.dropped == 3);
		{
			int oldest = 0;              /* the first three were replaced: the oldest left is number 3 */
			for (int k = 0; k < l.n; k++)
				if (l.dev[k].last_ms < l.dev[oldest].last_ms)
					oldest = k;
			CHECK(l.dev[oldest].addr[5] == 13);
		}
	}

	/* the beacon builders, read back with the parser */
	{
		uint8_t o[BT_ADV_MAX];
		uint8_t n = LeAdv_BuildName(o, "UV5R-MINI");
		CHECK(n == 3 + 2 + 9);
		CHECK(LeAdv_Parse(o, n, &in) && in.flags == 0x06 && in.name_full && !strcmp(in.name, "UV5R-MINI"));
		CHECK(LeAdv_BuildName(o, "A NAME FAR TOO LONG FOR ONE ADV") == 0);

		static const uint8_t want[] = { 2, 1, 6, 3, 3, 0xAA, 0xFE, 13, 0x16, 0xAA, 0xFE, 0x10, 0xEC,
		                                3, 'g', 'o', 'o', '.', 'g', 'l', 0x00 };  /* https:// goo.gl ".com/" */
		n = LeAdv_BuildEddystoneUrl(o, "https://goo.gl.com/", -20);
		CHECK(n == sizeof want && !memcmp(o, want, n));
		CHECK(LeAdv_Parse(o, n, &in) && in.kind == LE_KIND_EDDYSTONE);
		n = LeAdv_BuildEddystoneUrl(o, "http://www.example.org", 0);
		CHECK(n && o[13] == 0 && !memcmp(o + 14, "example", 7) && o[21] == 8 && o[7] == 14);
		CHECK(LeAdv_BuildEddystoneUrl(o, "ftp://x", 0) == 0);
		CHECK(LeAdv_BuildEddystoneUrl(o, "https://a-very-long-host-name-here.example", 0) == 0);

		static const uint8_t uuid[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
		n = LeAdv_BuildIBeacon(o, uuid, 0x1234, 0x5678, -59);
		CHECK(n == 30 && o[25] == 0x12 && o[26] == 0x34 && o[28] == 0x78 && (int8_t)o[29] == -59);
		CHECK(LeAdv_Parse(o, n, &in) && in.kind == LE_KIND_IBEACON);
	}

	/* the GATT server: a phone finds the serial service, switches notifications on and writes */
	{
		uint8_t o[GATT_MTU + 4];
		uint16_t cid;
		int n;
		GATT_Reset("UV5R-MINI", on_rx);
		static const uint8_t mtu[] = { 0x02, 0xF7, 0x00 };
		n = GATT_Input(L2CAP_CID_ATT, mtu, sizeof mtu, o, &cid);
		CHECK(n == 3 && cid == 4 && o[0] == 0x03 && o[1] == 23 && o[2] == 0);

		static const uint8_t grp[] = { 0x10, 0x01, 0x00, 0xFF, 0xFF, 0x00, 0x28 };
		n = GATT_Input(L2CAP_CID_ATT, grp, sizeof grp, o, &cid);
		CHECK(n == 2 + 6 && o[0] == 0x11 && o[1] == 6 && o[2] == 1 && o[4] == 5 && o[6] == 0x00 && o[7] == 0x18);
		static const uint8_t grp2[] = { 0x10, 0x06, 0x00, 0xFF, 0xFF, 0x00, 0x28 };
		n = GATT_Input(L2CAP_CID_ATT, grp2, sizeof grp2, o, &cid);
		CHECK(n == 2 + 20 && o[1] == 20 && o[2] == GATT_H_NUS && o[4] == GATT_H_LAST && o[6] == 0x9E && o[21] == 0x6E);
		static const uint8_t grp3[] = { 0x10, 0x0C, 0x00, 0xFF, 0xFF, 0x00, 0x28 };
		n = GATT_Input(L2CAP_CID_ATT, grp3, sizeof grp3, o, &cid);
		CHECK(n == 5 && o[0] == 0x01 && o[1] == 0x10 && o[4] == 0x0A);

		static const uint8_t chr[] = { 0x08, 0x06, 0x00, 0x0B, 0x00, 0x03, 0x28 };
		n = GATT_Input(L2CAP_CID_ATT, chr, sizeof chr, o, &cid);    /* 21-byte entries: one per answer */
		CHECK(n == 2 + 21 && o[1] == 21 && o[2] == GATT_H_RX_DECL && o[4] == 0x0C && o[5] == GATT_H_RX && o[22] == 0x6E);
		static const uint8_t chr2[] = { 0x08, 0x08, 0x00, 0x0B, 0x00, 0x03, 0x28 };
		n = GATT_Input(L2CAP_CID_ATT, chr2, sizeof chr2, o, &cid);
		CHECK(n == 23 && o[2] == GATT_H_TX_DECL && o[4] == 0x10 && o[5] == GATT_H_TX && o[19] == 0x03);

		static const uint8_t info[] = { 0x04, 0x0A, 0x00, 0x0B, 0x00 };
		n = GATT_Input(L2CAP_CID_ATT, info, sizeof info, o, &cid);    /* 128-bit first: one entry */
		CHECK(n == 2 + 18 && o[1] == 2 && o[2] == GATT_H_TX && o[19] == 0x6E);
		static const uint8_t info2[] = { 0x04, 0x0B, 0x00, 0x0B, 0x00 };
		n = GATT_Input(L2CAP_CID_ATT, info2, sizeof info2, o, &cid);
		CHECK(n == 6 && o[1] == 1 && o[2] == GATT_H_TX_CCCD && o[4] == 0x02 && o[5] == 0x29);

		static const uint8_t find[] = { 0x06, 0x01, 0x00, 0xFF, 0xFF, 0x00, 0x28,
		                                0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E };
		n = GATT_Input(L2CAP_CID_ATT, find, sizeof find, o, &cid);
		CHECK(n == 5 && o[0] == 0x07 && o[1] == GATT_H_NUS && o[3] == GATT_H_LAST);

		static const uint8_t rname[] = { 0x0A, GATT_H_NAME, 0x00 };
		n = GATT_Input(L2CAP_CID_ATT, rname, sizeof rname, o, &cid);
		CHECK(n == 10 && o[0] == 0x0B && !memcmp(o + 1, "UV5R-MINI", 9));
		static const uint8_t blob[] = { 0x0C, GATT_H_NAME, 0x00, 5, 0 };
		n = GATT_Input(L2CAP_CID_ATT, blob, sizeof blob, o, &cid);
		CHECK(n == 5 && o[0] == 0x0D && !memcmp(o + 1, "MINI", 4));
		static const uint8_t rtx[] = { 0x0A, GATT_H_TX, 0x00 };
		n = GATT_Input(L2CAP_CID_ATT, rtx, sizeof rtx, o, &cid);
		CHECK(n == 5 && o[0] == 0x01 && o[4] == 0x02);
		static const uint8_t rbad[] = { 0x0A, 0x40, 0x00 };
		n = GATT_Input(L2CAP_CID_ATT, rbad, sizeof rbad, o, &cid);
		CHECK(n == 5 && o[4] == 0x01);

		CHECK(!GATT_NotifyOn());
		static const uint8_t cccd[] = { 0x12, GATT_H_TX_CCCD, 0x00, 0x01, 0x00 };
		n = GATT_Input(L2CAP_CID_ATT, cccd, sizeof cccd, o, &cid);
		CHECK(n == 1 && o[0] == 0x13 && GATT_NotifyOn());

		static const uint8_t wr[] = { 0x52, GATT_H_RX, 0x00, 'H', 'I' };
		n = GATT_Input(L2CAP_CID_ATT, wr, sizeof wr, o, &cid);
		CHECK(n == 0 && rx_len == 2 && !memcmp(rx_buf, "HI", 2));
		static const uint8_t wreq[] = { 0x12, GATT_H_RX, 0x00, '!' };
		n = GATT_Input(L2CAP_CID_ATT, wreq, sizeof wreq, o, &cid);
		CHECK(n == 1 && o[0] == 0x13 && rx_len == 3 && rx_buf[2] == '!');
		static const uint8_t wname[] = { 0x12, GATT_H_NAME, 0x00, 'X' };
		n = GATT_Input(L2CAP_CID_ATT, wname, sizeof wname, o, &cid);
		CHECK(n == 5 && o[4] == 0x03);

		static const uint8_t odd[] = { 0x20, 0x01 };
		CHECK(GATT_Input(L2CAP_CID_ATT, odd, sizeof odd, o, &cid) == 5 && o[4] == 0x06);
		static const uint8_t cmd[] = { 0x60, 0x01 };
		CHECK(GATT_Input(L2CAP_CID_ATT, cmd, sizeof cmd, o, &cid) == 0);
		static const uint8_t conf[] = { 0x1E };
		CHECK(GATT_Input(L2CAP_CID_ATT, conf, sizeof conf, o, &cid) == 0);

		static const uint8_t pair[] = { 0x01, 0x03, 0x00, 0x01, 0x10, 0x07, 0x07 };
		n = GATT_Input(L2CAP_CID_SMP, pair, sizeof pair, o, &cid);
		CHECK(n == 2 && cid == 6 && o[0] == 0x05 && o[1] == 0x05);
		static const uint8_t sig[] = { 0x12, 0x07, 0x08, 0x00, 6, 0, 6, 0, 0, 0, 0x2C, 0x01 };
		n = GATT_Input(L2CAP_CID_SIG, sig, sizeof sig, o, &cid);
		CHECK(n == 6 && cid == 5 && o[0] == 0x01 && o[1] == 0x07 && o[4] == 0 && o[5] == 0);
		static const uint8_t sigrsp[] = { 0x13, 0x07, 0x02, 0x00, 0, 0 };
		CHECK(GATT_Input(L2CAP_CID_SIG, sigrsp, sizeof sigrsp, o, &cid) == 0);

		n = GATT_Notify((const uint8_t *)"0123456789ABCDEFGHIJKLMN", 24, o);
		CHECK(n == 23 && o[0] == 0x1B && o[1] == GATT_H_TX && o[22] == 'J');

		GATT_Reset("A-NAME-LONGER-THAN-20-CHARS", 0);
		CHECK(!GATT_NotifyOn());
		n = GATT_Input(L2CAP_CID_ATT, rname, sizeof rname, o, &cid);
		CHECK(n == 21);
		n = GATT_Input(L2CAP_CID_ATT, wr, sizeof wr, o, &cid);    /* no handler: dropped */
		CHECK(n == 0 && rx_len == 3);
	}

	printf("%d checks, %d failed\n", checks, fails);
	return fails != 0;
}
