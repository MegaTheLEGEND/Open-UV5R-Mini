/* Open-UV5R-Mini: PC test of src/bt/le_adv.c (tests/btle_host.py): BLE advertising data parsed, the device list. */
#include <stdio.h>
#include <string.h>
#include "bt/le_adv.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

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

	printf("%d checks, %d failed\n", checks, fails);
	return fails != 0;
}
