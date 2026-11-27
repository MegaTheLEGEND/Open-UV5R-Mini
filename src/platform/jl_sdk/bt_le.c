/* Open-UV5R-Mini: the bare-metal BLE backend (ENABLE_BT_LE): bt/bt.h on JieLi's Bluetooth controller library.
 *
 * The controller (btctrler.a, JieLi's public BR23 SDK, Apache-2.0, copied into build/sdk/ by tools/sdk/fm_libs.sh;
 * docs/THIRD-PARTY.md) is driven the way JieLi's own host drives it: commands through its direct calls
 * (ll_hci_scan_set_params, ll_hci_scan_enable, ll_hci_adv_set_*), events back as standard HCI event packets to
 * hci_packet_handler, which the host defines (here). The controller's task is run from our main loop
 * (BTLE_Tick: its message queue, then hci_controller_run_loop) instead of an OS task; bt_glue.c has the stand-ins.
 *
 * Bluetooth runs only while an app has it (scanning or advertising): started on the first request, stopped when
 * nothing needs it. Its RAM (the library's variables, our queue and the Bluetooth heap) is low RAM 0x3000..0xC000
 * ("btram", firmware.ld), set up fresh at every start. No pairing, no connections: scanning and non-connectable
 * advertising only. Nothing is written to flash. Not run on a radio yet (docs/BLUETOOTH.md). */
#include <stddef.h>
#include "bt/bt.h"
#include "bsp/ac695n.h"
#include "driver/system.h"
#include "platform/jl_sdk/bt_glue.h"
#include "platform/jl_sdk/bt_le.h"

/* ------------------------------------------------------------------ the library's calls we use */
extern const void *hci_transport_h4_controller_instance(void);
extern int  btctrler_task_init(const void *transport, const void *config);
extern void hci_controller_init(void);
extern void hci_controller_run_loop(void);
extern void ll_event_handler(int *msg);
extern void ll_hci_cmd_handler(int *msg);
extern void hci_controller_destory(void);
extern void bt_pll_para(uint32_t osc, uint32_t sys, uint8_t low_power, uint8_t xosc);
extern void bt_max_pwr_set(uint8_t pwr, uint8_t pg_pwr, uint8_t iq_pwr, uint8_t ble_pwr);
extern void dev_hci_set_event_mask(const uint8_t *mask);
extern void ll_hci_set_event_mask(const uint8_t *mask);
extern int  le_controller_set_mac(void *addr);
extern int  le_controller_set_random_mac(void *addr);
extern void ll_hci_scan_set_params(uint8_t scan_type, uint16_t interval, uint16_t window);
extern int  ll_hci_scan_enable(uint8_t enable, uint8_t filter_duplicates);
extern void ll_hci_adv_set_params(uint16_t min, uint16_t max, uint8_t type, uint8_t direct_type,
                                  const uint8_t *direct_addr, uint8_t channel_map, uint8_t filter);
extern void ll_hci_adv_set_data(uint8_t len, const uint8_t *data);
extern int  ll_hci_adv_enable(uint8_t enable);

/* the controller task's messages (SDK btctrler_task.h: Q_USER + n) */
#define MSG_LL_EVENT     0x400005
#define MSG_HCI_CMD_TO_LL 0x400006
#define MSG_TASK_READY   0x40000C

/* firmware.ld: the btram region's initialised part (copied from the flash) and its zeroed part */
extern uint8_t __btram_data_start[], __btram_data_end[], __btram_data_load[], __btram_bss_start[], __btram_bss_end[];
extern uint8_t __btram_heap_start[], __btram_heap_end[];

#define OSC_HZ   24000000u               /* the crystal */
#define BLE_PWR  7u                      /* BR23 BLE TX power level 0-9: 7 = +2.1 dBm (SDK table) */
#define SYS_HZ   48000000u               /* the CPU clock outside the FM radio (RE-NOTES: CLK_CON2 192 MHz / 4) */

/* ------------------------------------------------------------------ state */
#define REP_N 8
static struct {
	BT_LeReport_t rep[REP_N];
	uint8_t  rr, rw, rn;
	uint32_t reports, dropped, events;
	uint8_t  addr[6];                    /* our random static address, most significant byte first */
} R BTRAM_BSS;

static uint8_t s_on;                     /* the controller is running */
static uint8_t s_scan, s_adv;            /* what the apps asked for */
BtLeStats_t gBtLe;

/* ------------------------------------------------------------------ events from the controller */
static void report(const uint8_t *p, int n)  /* LE Advertising Report, after the subevent byte */
{
	int k = 1, num = n > 0 ? p[0] : 0;

	for (int i = 0; i < num && k + 9 <= n; i++) {
		const uint8_t *e = p + k;        /* event type, address type, address (LSB first), length, data, RSSI */
		uint8_t len = e[8];
		BT_LeReport_t *r;

		if (k + 9 + len + 1 > n)
			break;
		R.reports++;
		if (R.rn >= REP_N) {
			R.dropped++;
		} else {
			r = &R.rep[R.rw];
			r->type = e[0];
			r->addr_type = e[1] & 1u;
			for (int j = 0; j < 6; j++)
				r->addr[j] = e[7 - j];
			r->len = len > BT_ADV_MAX ? BT_ADV_MAX : len;
			for (int j = 0; j < r->len; j++)
				r->data[j] = e[9 + j];
			r->rssi = (int8_t)e[9 + len];
			R.rw = (uint8_t)((R.rw + 1u) % REP_N);
			R.rn++;
		}
		k += 10 + len;
	}
}

/* the host's receive side, called by the controller's transport (h4_controller_interface: type, packet, size) */
int hci_packet_handler(uint8_t type, const uint8_t *packet, uint16_t size)
{
	R.events++;
	gBtLe.events++;
	if (type != 4 || size < 2)           /* HCI event packets only */
		return 0;
	gBtLe.last_event = packet[0];
	if (packet[0] == 0x3E && size >= 3) {    /* LE Meta */
		gBtLe.last_sub = packet[2];
		if (packet[2] == 0x02)               /* LE Advertising Report */
			report(packet + 3, size - 3);
	}
	return 0;
}

/* ------------------------------------------------------------------ start and stop */
static void ram_setup(void)
{
	for (uint8_t *p = __btram_bss_start; p < __btram_bss_end; p++)
		*p = 0;
	for (uint8_t *d = __btram_data_start, *s = __btram_data_load; d < __btram_data_end; )
		*d++ = *s++;
}

static void make_address(void)
{
	/* a random static address (top two bits 11), the same every start: from the chip's flash ID and a constant */
	uint32_t a = 0x5556u ^ REG32(0x001E0040u), b = 0x4F50454Eu;
	for (int i = 0; i < 6; i++) {
		b = b * 1103515245u + 12345u + a;
		R.addr[i] = (uint8_t)(b >> 16);
	}
	R.addr[0] |= 0xC0u;
}

static bool start(void)
{
	static const uint8_t ALL[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x3F };
	static const uint8_t LE_ALL[8] = { 0x1F, 0, 0, 0, 0, 0, 0, 0 };
	uint8_t lsb[6];

	if (s_on)
		return true;
	gBtLe.step = 1;
	ram_setup();
	FMGLUE_BtHeap(true, __btram_heap_start, (uint32_t)(__btram_heap_end - __btram_heap_start));
	BTGLUE_QueueReset();
	BTGLUE_TimersReset(SYSTEM_Ms());
	make_address();

	/* the radio block's clocks and supply, as stock's Bluetooth start-up (sub_01E0173E) and our FM start set them:
	 * CLK_CON1 bits 14-15 (Bluetooth clock: PLL 48 MHz), 18-19 (radio ADC: PLL 96 MHz), 20-21 clear; VDC13 1.40 V */
	REG32(0x001E0010u) &= ~(3u << 14 | 3u << 18 | 3u << 20);
	P33_Write(6, (uint8_t)((P33_Read(6) & ~7u) | 7u));

	gBtLe.step = 2;
	bt_pll_para(OSC_HZ, SYS_HZ, 0, 0);
	bt_max_pwr_set(7, 5, 8, BLE_PWR);
	gBtLe.step = 3;
	btctrler_task_init(hci_transport_h4_controller_instance(), 0);  /* records the transport; no task is made */
	gBtLe.step = 4;
	IRQ_Init();                          /* a clean table: the controller asks for its own (38 receive, 39 events) */
	hci_controller_init();               /* RF, analog, link layer: what the SDK's controller task does first */
	IRQ_GlobalOn();
	gBtLe.step = 5;
	dev_hci_set_event_mask(ALL);
	ll_hci_set_event_mask(LE_ALL);
	for (int i = 0; i < 6; i++)
		lsb[i] = R.addr[5 - i];
	le_controller_set_random_mac(lsb);
	le_controller_set_mac(lsb);
	s_on = 1;
	gBtLe.step = 6;
	gBtLe.starts++;
	return true;
}

static void stop(void)
{
	if (!s_on)
		return;
	gBtLe.step = 20;
	ll_hci_scan_enable(0, 0);
	ll_hci_adv_enable(0);
	BTLE_Tick();                         /* let the controller take the two commands */
	hci_controller_destory();
	IRQ_Free(38);
	IRQ_Free(39);
	s_on = 0;
	FMGLUE_BtHeap(false, 0, 0);
	gBtLe.step = 0;
}

static void stop_if_idle(void)
{
	if (!s_scan && !s_adv)
		stop();
}

/* ------------------------------------------------------------------ the main loop's part */
void BTLE_Tick(void)
{
	int msg[14];

	if (!s_on)
		return;
	BTGLUE_TimersRun(SYSTEM_Ms());
	for (int n = 0; n < 16 && os_taskq_pend(0, msg, 14) == 13; n++) {
		gBtLe.msgs++;
		switch (msg[0]) {
		case MSG_LL_EVENT:      ll_event_handler(msg + 1); break;
		case MSG_HCI_CMD_TO_LL: ll_hci_cmd_handler(msg + 1); break;
		default:                gBtLe.msgs_other++; break;
		}
		hci_controller_run_loop();
	}
	hci_controller_run_loop();
}

/* ------------------------------------------------------------------ bt/bt.h */
uint8_t     BT_Caps(void) { return BT_CAP_LE_SCAN | BT_CAP_LE_ADV; }
const char *BT_Backend(void) { return "JL BLE"; }

bool BT_GetAddress(uint8_t addr[6])
{
	if (!s_on)
		return false;
	for (int i = 0; i < 6; i++)
		addr[i] = R.addr[i];
	return true;
}

bool BT_IsAvailable(void) { return false; }   /* no serial link in this backend */
void BT_Init(BT_ReceiveHandler_t on_receive) { (void)on_receive; }
bool BT_IsConnected(void) { return false; }
int  BT_Send(const uint8_t *data, uint16_t len) { (void)data; (void)len; return -1; }

bool BT_LeScanStart(bool active)
{
	if (!start())
		return false;
	R.rr = R.rw = R.rn = 0;
	/* interval 100 ms, window 50 ms (0.625 ms units); active: scan requests for the scan responses (names) */
	ll_hci_scan_set_params(active ? 1u : 0u, 160u, 80u);
	ll_hci_scan_enable(1, 0);
	s_scan = 1;
	BTLE_Tick();
	return true;
}

void BT_LeScanStop(void)
{
	if (!s_scan)
		return;
	s_scan = 0;
	if (s_on) {
		ll_hci_scan_enable(0, 0);
		BTLE_Tick();
	}
	stop_if_idle();
}

bool BT_LePoll(BT_LeReport_t *out)
{
	uint32_t w;

	BTLE_Tick();
	if (!s_on)
		return false;
	w = BTGLUE_IrqSave();
	if (!R.rn) {
		BTGLUE_IrqRestore(w);
		return false;
	}
	*out = R.rep[R.rr];
	R.rr = (uint8_t)((R.rr + 1u) % REP_N);
	R.rn--;
	BTGLUE_IrqRestore(w);
	return true;
}

bool BT_LeAdvStart(const uint8_t *data, uint8_t len, uint16_t interval_ms)
{
	uint16_t ivl;

	if (len > BT_ADV_MAX || interval_ms < 20u || interval_ms > 10240u)
		return false;
	if (!start())
		return false;
	ivl = (uint16_t)((uint32_t)interval_ms * 8u / 5u);   /* 0.625 ms units */
	ll_hci_adv_enable(0);
	/* ADV_NONCONN_IND (3), own address random, all three channels, no filter */
	ll_hci_adv_set_params(ivl, ivl, 3, 0, (const uint8_t *)"\0\0\0\0\0\0", 7, 0);
	ll_hci_adv_set_data(len, data);
	ll_hci_adv_enable(1);
	s_adv = 1;
	BTLE_Tick();
	return true;
}

void BT_LeAdvStop(void)
{
	if (!s_adv)
		return;
	s_adv = 0;
	if (s_on) {
		ll_hci_adv_enable(0);
		BTLE_Tick();
	}
	stop_if_idle();
}

bool BTLE_IsOn(void) { return s_on; }
uint32_t BTLE_Reports(void) { return R.reports; }
