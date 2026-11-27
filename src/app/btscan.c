/* Open-UV5R-Mini: BLE SCANNER (ENABLE_APP_BTSCAN), after Flipper Zero's Bluetooth scanner: MENU -> APPS -> BLE SCAN.
 * Receive only: it lists the Bluetooth Low Energy devices advertising nearby, strongest first, with their name (or
 * address), signal and a guess at what they are (bt/le_adv.c); MENU shows one device in full.
 *
 * It needs a Bluetooth backend that can scan (bt/bt.h BT_CAP_LE_SCAN). The open build has none yet, so it says so.
 * Debug builds take made-up reports from the cable monitor ("!app adv ..."), which tests the list and the screens on
 * a radio before there is a backend.
 *
 * Keys: UP/DOWN pick, MENU details (and back), 1 active / passive scanning, 0 clear, EXIT back.
 * All of its state is in .ram2 (the stock app's RAM, free under our firmware): .bss is full. */
#include "app/apps.h"
#if APP_BTSCAN_ENABLE
#include "app/app.h"
#include "app/monitor.h"
#include "bt/bt.h"
#include "bt/le_adv.h"
#include "driver/st7735s.h"
#if BT_LE_ENABLE
#include "platform/jl_sdk/bt_le.h"
#endif
#include "ui/helper.h"
#include "ui/status.h"
#include "ui/theme.h"

#define ROW_Y(r)   ((uint8_t)(13u + 10u * (r)))
#define ROWS       10                    /* list rows on the screen */
#define AGE_MS     30000u                /* a device not heard for this long leaves the list */
#define REDRAW_MS  500u

static struct {
	LeList_t list;
	uint8_t  order[LE_LIST_MAX];
	uint8_t  n;                          /* in order */
	uint8_t  sel;                        /* index into order */
	uint8_t  top;                        /* first row shown */
	bool     detail;
	bool     active;
	bool     scanning;                   /* the backend accepted the scan */
	bool     redraw, full;
	uint16_t ms;
	uint8_t  sel_addr[6];                /* the device picked, kept across re-sorting */
	bool     has_sel;
} S __attribute__((section(".ram2.btscan")));

static char *put(char *o, const char *s) { while (*s) *o++ = *s++; *o = 0; return o; }

static char *hex2(char *o, uint8_t v)
{
	static const char H[] = "0123456789ABCDEF";
	*o++ = H[v >> 4];
	*o++ = H[v & 15];
	*o = 0;
	return o;
}

static void row(uint8_t r, const char *t, uint16_t fg) { UI_PrintPadded(t, 2, ROW_Y(r), fg, COLOR_BG, 21); }

static bool addr_eq(const uint8_t *a, const uint8_t *b)
{
	for (int i = 0; i < 6; i++)
		if (a[i] != b[i])
			return false;
	return true;
}

/* re-sort, keeping the picked device picked */
static void resort(void)
{
	S.n = (uint8_t)LeList_Sorted(&S.list, S.order);
	if (S.has_sel)
		for (uint8_t i = 0; i < S.n; i++)
			if (addr_eq(S.list.dev[S.order[i]].addr, S.sel_addr)) {
				S.sel = i;
				break;
			}
	if (S.sel >= S.n)
		S.sel = S.n ? (uint8_t)(S.n - 1u) : 0;
	if (S.n) {
		for (int i = 0; i < 6; i++)
			S.sel_addr[i] = S.list.dev[S.order[S.sel]].addr[i];
		S.has_sel = true;
	} else {
		S.has_sel = false;
		S.detail = false;
	}
	if (S.sel < S.top)
		S.top = S.sel;
	if (S.sel >= S.top + ROWS)
		S.top = (uint8_t)(S.sel - ROWS + 1u);
}

static void start_scan(void)
{
	BT_LeScanStop();
	S.scanning = (BT_Caps() & BT_CAP_LE_SCAN) && BT_LeScanStart(S.active);
}

/* ------------------------------------------------------------------ drawing */
static void draw_list(void)
{
	char b[24], *o;

	if (!S.n) {
		if (!(BT_Caps() & BT_CAP_LE_SCAN)) {
			row(0, "NO BLE SCANNING IN", COLOR_WARN);
			row(1, "THIS BUILD.", COLOR_WARN);
			row(2, "", COLOR_TEXT);
			row(3, "THE BLUETOOTH LINK", COLOR_DIM);
			row(4, "TO JIELI'S RADIO IS", COLOR_DIM);
			row(5, "NOT WRITTEN YET:", COLOR_DIM);
			row(6, "DOCS/BLUETOOTH.MD", COLOR_DIM);
			for (uint8_t r = 7; r < ROWS; r++)
				row(r, "", COLOR_TEXT);
		} else {
			row(0, S.scanning ? "LISTENING..." : "SCAN FAILED TO START", S.scanning ? COLOR_DIM : COLOR_WARN);
			for (uint8_t r = 1; r < ROWS; r++)
				row(r, "", COLOR_TEXT);
		}
		return;
	}
	for (uint8_t r = 0; r < ROWS; r++) {
		uint8_t i = (uint8_t)(S.top + r);
		const LeDevice_t *d;
		int k;

		if (i >= S.n) {
			row(r, "", COLOR_TEXT);
			continue;
		}
		d = &S.list.dev[S.order[i]];
		o = b;
		if (d->info.name[0])
			o = put(o, d->info.name);
		else if (d->info.kind)
			o = put(o, LeAdv_KindName(d->info.kind));
		else {                           /* the address's last four bytes */
			for (k = 2; k < 6; k++)
				o = hex2(o, d->addr[k]);
		}
		k = (int)(o - b);
		while (k < 16)
			b[k++] = ' ';
		o = b + 16;
		if (d->rssi > -100)
			*o++ = ' ';
		if (d->rssi > -10)
			*o++ = ' ';
		APPS_PutInt(o, d->rssi);
		row(r, b, i == S.sel ? COLOR_ACCENT : COLOR_TEXT);
	}
}

static void draw_detail(void)
{
	const LeDevice_t *d = &S.list.dev[S.order[S.sel]];
	static const char *const TYPES[] = { "ADV_IND", "DIRECT", "SCAN_IND", "BEACON", "SCAN_RSP" };
	char b[24], *o;
	const char *c;
	uint8_t r = 0;

	o = put(b, d->info.name[0] ? d->info.name : "(NO NAME)");
	row(r++, b, COLOR_ACCENT);
	LeAdv_FormatAddr(b, d->addr);
	row(r++, b, COLOR_TEXT);
	o = put(b, d->addr_type ? "RANDOM " : "PUBLIC ");
	put(o, d->type < 5 ? TYPES[d->type] : "?");
	row(r++, b, COLOR_DIM);
	o = put(b, "RSSI ");
	o = APPS_PutInt(o, d->rssi);
	o = put(o, " MAX ");
	o = APPS_PutInt(o, d->rssi_max);
	if (d->info.has_tx) {
		o = put(o, " TX ");
		APPS_PutInt(o, d->info.tx_dbm);
	}
	row(r++, b, COLOR_TEXT);
	o = put(b, "SEEN ");
	o = APPS_PutInt(o, d->count);
	o = put(o, "X  ");
	o = APPS_PutInt(o, (int)((gAppsUptimeMs - d->last_ms) / 1000u));
	put(o, "S AGO");
	row(r++, b, COLOR_TEXT);
	o = b;
	*o = 0;
	if (d->info.kind)
		o = put(o, LeAdv_KindName(d->info.kind));
	if (d->info.has_company) {
		if (o != b)
			*o++ = ' ';
		c = LeAdv_CompanyName(d->info.company);
		if (c && d->info.kind != LE_KIND_APPLE && d->info.kind != LE_KIND_MICROSOFT &&
		    d->info.kind != LE_KIND_GOOGLE && d->info.kind != LE_KIND_SAMSUNG)
			o = put(o, c);
		else if (!c) {
			o = put(o, "CO ");
			o = hex2(o, (uint8_t)(d->info.company >> 8));
			o = hex2(o, (uint8_t)d->info.company);
		}
	}
	if (d->info.uuid16) {
		o = put(o, " SVC ");
		o = hex2(o, (uint8_t)(d->info.uuid16 >> 8));
		hex2(o, (uint8_t)d->info.uuid16);
	}
	row(r++, b, COLOR_INFO);
	for (uint8_t i = 0; r < ROWS; r++) {    /* the raw data, 10 bytes a row */
		o = b;
		*o = 0;
		for (uint8_t k = 0; k < 10u && i < d->len; k++, i++)
			o = hex2(o, d->data[i]);
		row(r, b, COLOR_DIM);
	}
}

static void draw(bool full)
{
	char t[10], *o;

	if (full) {
		ST7735S_FillRect(0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_BG);
		UI_TitleBar(S.detail ? "BLE DEVICE" : "BLE SCAN", "");
	}
	o = APPS_PutInt(t, S.n);
	put(o, S.active ? " ACT" : " PAS");
	UI_TitleRight(t, COLOR_DIM);
	if (S.detail && S.n)
		draw_detail();
	else
		draw_list();
}

/* ------------------------------------------------------------------ the cable monitor (debug builds) */
#if DEBUGGING
static bool mword(const char **s, const char *w)
{
	const char *p = *s;
	while (*w)
		if (*p++ != *w++)
			return false;
	if (*p && *p != ' ')
		return false;
	while (*p == ' ')
		p++;
	*s = p;
	return true;
}

static int hexval(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	return -1;
}

/* hex bytes up to a space or the end; returns how many, or -1 */
static int mhex(const char **s, uint8_t *out, int max)
{
	const char *p = *s;
	int n = 0;
	while (*p && *p != ' ') {
		int h = hexval(p[0]), l = hexval(p[1]);
		if (h < 0 || l < 0 || n >= max)
			return -1;
		out[n++] = (uint8_t)(h << 4 | l);
		p += 2;
	}
	while (*p == ' ')
		p++;
	*s = p;
	return n;
}

static bool mon(const char *a)
{
	if (mword(&a, "adv")) {              /* adv ADDR12HEX RSSI TYPE [DATAHEX]: a made-up report */
		BT_LeReport_t r = { { 0 }, 1, BT_ADV_IND, 0, 0, { 0 } };
		int v = 0, neg = 0, n;
		if (mhex(&a, r.addr, 6) != 6)
			return false;
		if (*a == '-')
			neg = 1, a++;
		while (*a >= '0' && *a <= '9')
			v = v * 10 + (*a++ - '0');
		while (*a == ' ')
			a++;
		r.rssi = (int8_t)(neg ? -v : v);
		if (*a >= '0' && *a <= '4')
			r.type = (uint8_t)(*a++ - '0');
		while (*a == ' ')
			a++;
		if ((n = mhex(&a, r.data, BT_ADV_MAX)) < 0)
			return false;
		r.len = (uint8_t)n;
		LeList_Add(&S.list, &r, gAppsUptimeMs);
		resort();
		S.redraw = true;
		return true;
	}
	if (mword(&a, "list")) {
		char b[64], *o;
		for (uint8_t i = 0; i < S.n; i++) {
			const LeDevice_t *d = &S.list.dev[S.order[i]];
			o = put(b, "@");
			o = LeAdv_FormatAddr(o, d->addr);
			*o++ = ' ';
			o = APPS_PutInt(o, d->rssi);
			o = put(o, " n=");
			o = APPS_PutInt(o, d->count);
			o = put(o, " ");
			o = put(o, d->info.name);
			o = put(o, " ");
			put(o, LeAdv_KindName(d->info.kind));
			MON_Line(b);
		}
		return true;
	}
#if BT_LE_ENABLE
	if (mword(&a, "bt")) {               /* the BLE backend: start step, events, messages, reports */
		char b[64], *o = put(b, "@step ");
		o = APPS_PutInt(o, gBtLe.step);
		o = put(o, " starts ");
		o = APPS_PutInt(o, (int)gBtLe.starts);
		o = put(o, " events ");
		o = APPS_PutInt(o, (int)gBtLe.events);
		o = put(o, " last ");
		o = APPS_PutInt(o, gBtLe.last_event);
		*o++ = '/';
		o = APPS_PutInt(o, gBtLe.last_sub);
		o = put(o, " msgs ");
		o = APPS_PutInt(o, (int)gBtLe.msgs);
		o = put(o, " reports ");
		APPS_PutInt(o, (int)BTLE_Reports());
		MON_Line(b);
		return true;
	}
#endif
	if (mword(&a, "clear")) {
		LeList_Clear(&S.list);
		resort();
		S.redraw = S.full = true;
		return true;
	}
	MON_Line("@app: adv ADDR12HEX RSSI [TYPE0-4] [DATAHEX], list, clear, bt");
	return mword(&a, "help");
}
#endif

/* ------------------------------------------------------------------ the app */
static void enter(void)
{
	LeList_Clear(&S.list);
	S.n = S.sel = S.top = 0;
	S.detail = false;
	S.has_sel = false;
	S.active = true;
	S.ms = 0;
	S.redraw = S.full = true;
	start_scan();
#if DEBUGGING
	gMonApp = mon;
#endif
}

static void leave(void)
{
#if DEBUGGING
	gMonApp = 0;
#endif
	BT_LeScanStop();
	S.scanning = false;
}

static AppsResult_t key(KEY_Code_t k)
{
	switch (k) {
	case KEY_EXIT:
		if (S.detail) {
			S.detail = false;
			S.redraw = S.full = true;
			return APPS_STAY;
		}
		return APPS_EXIT;
	case KEY_UP:
	case KEY_DOWN:
		if (S.n) {
			if (k == KEY_UP)
				S.sel = S.sel ? (uint8_t)(S.sel - 1u) : (uint8_t)(S.n - 1u);
			else
				S.sel = (uint8_t)(S.sel + 1u < S.n ? S.sel + 1u : 0u);
			for (int i = 0; i < 6; i++)
				S.sel_addr[i] = S.list.dev[S.order[S.sel]].addr[i];
			S.has_sel = true;
			resort();
			S.redraw = true;
			S.full = S.detail;
		}
		break;
	case KEY_MENU:
		if (S.n || S.detail) {
			S.detail = !S.detail;
			S.redraw = S.full = true;
		}
		break;
	case KEY_1:
		S.active = !S.active;
		start_scan();
		S.redraw = true;
		break;
	case KEY_0:
		LeList_Clear(&S.list);
		resort();
		S.redraw = S.full = true;
		break;
	default:
		break;
	}
	return APPS_STAY;
}

static AppsResult_t tick(bool full)
{
	BT_LeReport_t r;
	uint8_t got = 0;

	while (got < 16u && BT_LePoll(&r)) { /* a bounded number a tick: the keys stay quick */
		LeList_Add(&S.list, &r, gAppsUptimeMs);
		got++;
	}
	if ((S.ms = (uint16_t)(S.ms + APP_TICK_MS)) >= REDRAW_MS) {
		uint8_t was = S.list.n;
		S.ms = 0;
		LeList_Age(&S.list, gAppsUptimeMs, AGE_MS);
		if (got || was != S.list.n || S.detail)
			S.redraw = true;
		resort();
	}
	if (full || S.full)
		draw(true);
	else if (S.redraw)
		draw(false);
	S.redraw = S.full = false;
	return APPS_STAY;
}

const AppDef_t APP_BTSCAN = { "BLE SCAN", enter, key, tick, leave, false, false,
                              "UP/DOWN PICK\nMENU DETAILS\n1 ACTIVE/PASSIVE\n0 CLEAR LIST\nEXIT BACK" };
#endif
