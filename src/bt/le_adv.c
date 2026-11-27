/* Open-UV5R-Mini: BLE advertising data and the list of devices heard (bt/le_adv.h). Our own code, written from the
 * Bluetooth Core Specification's AD types and the Bluetooth SIG's assigned numbers (company IDs, 16-bit UUIDs). */
#include "bt/le_adv.h"

/* AD types */
#define AD_FLAGS       0x01
#define AD_UUID16_PART 0x02
#define AD_UUID16_ALL  0x03
#define AD_NAME_SHORT  0x08
#define AD_NAME_FULL   0x09
#define AD_TX_POWER    0x0A
#define AD_SVC_DATA16  0x16
#define AD_MANUFACTURER 0xFF

/* company IDs (Bluetooth SIG assigned numbers) */
#define CO_MICROSOFT 0x0006
#define CO_APPLE     0x004C
#define CO_SAMSUNG   0x0075
#define CO_GOOGLE    0x00E0

static const struct { uint16_t id; const char *name; } COMPANIES[] = {
	{ CO_MICROSOFT, "MICROSOFT" },
	{ CO_APPLE,     "APPLE" },
	{ CO_SAMSUNG,   "SAMSUNG" },
	{ CO_GOOGLE,    "GOOGLE" },
	{ 0x0059,       "NORDIC" },
	{ 0x000D,       "TI" },
	{ 0x0087,       "GARMIN" },
	{ 0x0157,       "HUAMI" },
	{ 0x038F,       "XIAOMI" },
	{ 0x05D6,       "JIELI" },
};

static const char *const KINDS[LE_KIND_COUNT] = {
	"", "IBEACON", "EDDYSTONE", "APPLE", "MICROSOFT", "GOOGLE", "SAMSUNG", "TILE", "HID", "HEART RATE",
};

const char *LeAdv_KindName(uint8_t kind) { return kind < LE_KIND_COUNT ? KINDS[kind] : ""; }

const char *LeAdv_CompanyName(uint16_t id)
{
	for (unsigned i = 0; i < sizeof COMPANIES / sizeof COMPANIES[0]; i++)
		if (COMPANIES[i].id == id)
			return COMPANIES[i].name;
	return 0;
}

char *LeAdv_FormatAddr(char *b, const uint8_t addr[6])
{
	static const char HEX[] = "0123456789ABCDEF";
	for (int i = 0; i < 6; i++) {
		*b++ = HEX[addr[i] >> 4];
		*b++ = HEX[addr[i] & 15];
		if (i < 5)
			*b++ = ':';
	}
	*b = 0;
	return b;
}

static uint8_t kind_of_uuid(uint16_t u)
{
	switch (u) {
	case 0xFEAA: return LE_KIND_EDDYSTONE;
	case 0xFE2C: return LE_KIND_GOOGLE;
	case 0xFEED: case 0xFEEC: return LE_KIND_TILE;
	case 0x1812: return LE_KIND_HID;
	case 0x180D: return LE_KIND_HEART;
	default: return LE_KIND_OTHER;
	}
}

bool LeAdv_Parse(const uint8_t *d, uint8_t len, LeAdvInfo_t *out)
{
	uint8_t i = 0;

	*out = (LeAdvInfo_t){ 0 };
	if (len > BT_ADV_MAX)
		len = BT_ADV_MAX;
	while (i < len) {
		uint8_t n = d[i];                /* bytes after the length byte: type + data */
		const uint8_t *p;
		uint8_t t, m;

		if (!n)                          /* zero length: the end (the rest is padding) */
			break;
		if ((unsigned)i + 1u + n > len) {
			out->bad = true;
			break;
		}
		t = d[i + 1];
		p = d + i + 2;
		m = (uint8_t)(n - 1u);           /* data bytes */
		switch (t) {
		case AD_FLAGS:
			if (m >= 1)
				out->flags = p[0];
			break;
		case AD_NAME_FULL:
		case AD_NAME_SHORT:
			if (out->name_full && t == AD_NAME_SHORT)
				break;                   /* keep the complete one */
			{
				uint8_t k = 0;
				for (; k < m && k < LE_NAME_MAX; k++)
					out->name[k] = p[k] >= 0x20 && p[k] < 0x7F ? (char)p[k] : '?';
				out->name[k] = 0;
				out->name_full = t == AD_NAME_FULL;
			}
			break;
		case AD_TX_POWER:
			if (m >= 1) {
				out->has_tx = true;
				out->tx_dbm = (int8_t)p[0];
			}
			break;
		case AD_UUID16_PART:
		case AD_UUID16_ALL:
		case AD_SVC_DATA16:
			if (m >= 2 && !out->uuid16)
				out->uuid16 = (uint16_t)(p[0] | p[1] << 8);
			break;
		case AD_MANUFACTURER:
			if (m >= 2 && !out->has_company) {
				out->has_company = true;
				out->company = (uint16_t)(p[0] | p[1] << 8);
				if (out->company == CO_APPLE && m >= 4 && p[2] == 0x02 && p[3] == 0x15)
					out->kind = LE_KIND_IBEACON;
			}
			break;
		default:
			break;
		}
		i = (uint8_t)(i + 1u + n);
	}

	if (out->kind == LE_KIND_OTHER && out->uuid16)
		out->kind = kind_of_uuid(out->uuid16);
	if (out->kind == LE_KIND_OTHER && out->has_company)
		switch (out->company) {
		case CO_APPLE:     out->kind = LE_KIND_APPLE; break;
		case CO_MICROSOFT: out->kind = LE_KIND_MICROSOFT; break;
		case CO_GOOGLE:    out->kind = LE_KIND_GOOGLE; break;
		case CO_SAMSUNG:   out->kind = LE_KIND_SAMSUNG; break;
		default: break;
		}
	return !out->bad;
}

/* ------------------------------------------------------------------ the devices heard */
void LeList_Clear(LeList_t *l)
{
	l->n = 0;
	l->reports = 0;
	l->dropped = 0;
}

static bool same_addr(const LeDevice_t *e, const BT_LeReport_t *r)
{
	for (int i = 0; i < 6; i++)
		if (e->addr[i] != r->addr[i])
			return false;
	return e->addr_type == r->addr_type;
}

int LeList_Add(LeList_t *l, const BT_LeReport_t *r, uint32_t now_ms)
{
	LeDevice_t *e = 0;
	LeAdvInfo_t in;
	int k;

	l->reports++;
	for (k = 0; k < l->n; k++)
		if (same_addr(&l->dev[k], r)) {
			e = &l->dev[k];
			break;
		}
	if (!e) {
		if (l->n < LE_LIST_MAX)
			k = l->n++;
		else {                           /* full: replace the one heard longest ago */
			k = 0;
			for (int j = 1; j < l->n; j++)
				if ((int32_t)(l->dev[j].last_ms - l->dev[k].last_ms) < 0)
					k = j;
			l->dropped++;
		}
		e = &l->dev[k];
		*e = (LeDevice_t){ 0 };
		for (int i = 0; i < 6; i++)
			e->addr[i] = r->addr[i];
		e->addr_type = r->addr_type;
		e->rssi_max = r->rssi;
		e->first_ms = now_ms;
		e->type = r->type;
	}
	e->rssi = r->rssi;
	if (r->rssi > e->rssi_max)
		e->rssi_max = r->rssi;
	if (e->count < 0xFFFFu)
		e->count++;
	e->last_ms = now_ms;

	LeAdv_Parse(r->data, r->len, &in);
	if (r->type == BT_ADV_SCAN_RSP) {    /* fills in, never replaces the advertisement */
		if (in.name[0] && (!e->info.name[0] || (in.name_full && !e->info.name_full))) {
			for (int i = 0; i <= LE_NAME_MAX; i++)
				e->info.name[i] = in.name[i];
			e->info.name_full = in.name_full;
		}
		if (!e->info.has_tx && in.has_tx) {
			e->info.has_tx = true;
			e->info.tx_dbm = in.tx_dbm;
		}
		if (!e->info.uuid16 && in.uuid16)
			e->info.uuid16 = in.uuid16;
		if (!e->info.has_company && in.has_company) {
			e->info.has_company = true;
			e->info.company = in.company;
		}
		if (e->info.kind == LE_KIND_OTHER)
			e->info.kind = in.kind;
	} else {
		char keep[LE_NAME_MAX + 1];
		bool keep_full = e->info.name_full;
		for (int i = 0; i <= LE_NAME_MAX; i++)
			keep[i] = e->info.name[i];
		e->info = in;
		if (!in.name[0] && keep[0]) {    /* a name from an earlier scan response stays */
			for (int i = 0; i <= LE_NAME_MAX; i++)
				e->info.name[i] = keep[i];
			e->info.name_full = keep_full;
		}
		e->type = r->type;
		e->len = r->len > BT_ADV_MAX ? BT_ADV_MAX : r->len;
		for (int i = 0; i < e->len; i++)
			e->data[i] = r->data[i];
	}
	return k;
}

void LeList_Age(LeList_t *l, uint32_t now_ms, uint32_t max_age_ms)
{
	int w = 0;
	for (int k = 0; k < l->n; k++)
		if (now_ms - l->dev[k].last_ms <= max_age_ms) {
			if (w != k)
				l->dev[w] = l->dev[k];
			w++;
		}
	l->n = (uint8_t)w;
}

int LeList_Sorted(const LeList_t *l, uint8_t order[LE_LIST_MAX])
{
	int n = l->n;
	for (int k = 0; k < n; k++)
		order[k] = (uint8_t)k;
	for (int i = 1; i < n; i++) {        /* insertion sort: 16 at most */
		uint8_t v = order[i];
		const LeDevice_t *a = &l->dev[v];
		int j = i - 1;
		while (j >= 0) {
			const LeDevice_t *b = &l->dev[order[j]];
			if (b->rssi > a->rssi || (b->rssi == a->rssi && (int32_t)(b->last_ms - a->last_ms) >= 0))
				break;
			order[j + 1] = order[j];
			j--;
		}
		order[j + 1] = v;
	}
	return n;
}

/* ------------------------------------------------------------------ the radio's own beacon */
static uint8_t put_flags(uint8_t *o)
{
	o[0] = 2;
	o[1] = AD_FLAGS;
	o[2] = 0x06;
	return 3;
}

uint8_t LeAdv_BuildName(uint8_t *out, const char *name)
{
	uint8_t n = put_flags(out), k = 0;
	while (name[k])
		k++;
	if (n + 2u + k > BT_ADV_MAX)
		return 0;
	out[n++] = (uint8_t)(k + 1u);
	out[n++] = AD_NAME_FULL;
	for (uint8_t i = 0; i < k; i++)
		out[n++] = (uint8_t)name[i];
	return n;
}

static bool starts(const char *s, const char *p)
{
	while (*p)
		if (*s++ != *p++)
			return false;
	return true;
}

uint8_t LeAdv_BuildEddystoneUrl(uint8_t *out, const char *url, int8_t tx_dbm)
{
	/* the URL scheme prefixes and the expansion codes of the Eddystone-URL format */
	static const char *const SCHEMES[] = { "http://www.", "https://www.", "http://", "https://" };
	static const char *const CODES[] = { ".com/", ".org/", ".edu/", ".net/", ".info/", ".biz/", ".gov/",
	                                     ".com", ".org", ".edu", ".net", ".info", ".biz", ".gov" };
	uint8_t n = put_flags(out), len_at, s;

	out[n++] = 3;                        /* the complete 16-bit service list: Eddystone */
	out[n++] = AD_UUID16_ALL;
	out[n++] = 0xAA;
	out[n++] = 0xFE;
	len_at = n++;
	out[n++] = AD_SVC_DATA16;
	out[n++] = 0xAA;
	out[n++] = 0xFE;
	out[n++] = 0x10;                     /* URL frame */
	out[n++] = (uint8_t)tx_dbm;
	for (s = 0; s < 4; s++)
		if (starts(url, SCHEMES[s]))
			break;
	if (s == 4)
		return 0;
	out[n++] = s;
	url += starts(url, "http://www.") ? 11 : starts(url, "https://www.") ? 12 : starts(url, "http://") ? 7 : 8;
	while (*url) {
		uint8_t c;
		for (c = 0; c < sizeof CODES / sizeof CODES[0]; c++)
			if (starts(url, CODES[c]))
				break;
		if (n >= BT_ADV_MAX)
			return 0;
		if (c < sizeof CODES / sizeof CODES[0]) {
			out[n++] = c;
			for (const char *k = CODES[c]; *k; k++)   /* skip the text the code stands for */
				url++;
		} else {
			if ((uint8_t)*url <= 0x20 || (uint8_t)*url >= 0x7F)
				return 0;
			out[n++] = (uint8_t)*url++;
		}
	}
	out[len_at] = (uint8_t)(n - len_at - 1u);
	return n;
}

uint8_t LeAdv_BuildIBeacon(uint8_t *out, const uint8_t uuid[16], uint16_t major, uint16_t minor, int8_t rssi_1m)
{
	uint8_t n = put_flags(out);
	out[n++] = 26;
	out[n++] = AD_MANUFACTURER;
	out[n++] = (uint8_t)CO_APPLE;
	out[n++] = (uint8_t)(CO_APPLE >> 8);
	out[n++] = 0x02;
	out[n++] = 0x15;
	for (int i = 0; i < 16; i++)
		out[n++] = uuid[i];
	out[n++] = (uint8_t)(major >> 8);
	out[n++] = (uint8_t)major;
	out[n++] = (uint8_t)(minor >> 8);
	out[n++] = (uint8_t)minor;
	out[n++] = (uint8_t)rssi_1m;
	return n;
}
