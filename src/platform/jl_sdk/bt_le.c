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
 * ("btram", firmware.ld), set up fresh at every start. Scanning, non-connectable advertising (beacons), and one
 * connection as a peripheral for the serial link (BT_Init): connectable advertising as "UV5R-MINI" with the Nordic
 * UART Service, whose GATT server is bt/gatt.c. Pairing (bt/smp.h): legacy pairing in pairing mode, passkey on
 * the screen, keys made again from a secret instead of stored. Nothing is written to flash (docs/BLUETOOTH.md). */
#include <stddef.h>
#include "bt/bt.h"
#include "bt/gatt.h"
#include "bt/smp.h"
#include "driver/flash.h"
#include "store/calib.h"
#include "bsp/ac695n.h"
#include "driver/system.h"
#include "platform/jl_sdk/bt_glue.h"
#include "platform/jl_sdk/bt_le.h"
#if APP_FM_ENABLE
#include "platform/jl_sdk/fmrx.h"
#endif

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
extern void ll_hci_adv_scan_response_set_data(uint8_t len, const uint8_t *data);
extern int  ll_hci_disconnect(uint16_t handle, uint8_t reason);
extern int  le_hci_send_acl_packet(const uint8_t *packet, uint16_t size);
extern int  ll_hci_long_term_key_request_reply(uint16_t handle, const uint8_t *ltk);
extern int  ll_hci_long_term_key_request_nagative_reply(uint16_t handle);

/* the serial service only for paired phones (features.mk ENABLE_BT_PAIRED_ONLY) */
#ifndef BT_PAIRED_ONLY
#define BT_PAIRED_ONLY 0
#endif

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
	/* L2CAP frames received on the connection, handled in BTLE_Tick (the main loop) */
	uint8_t  in[4][GATT_MTU + 8];
	uint8_t  in_len[4];
	uint8_t  ir, iw, in_n;
	uint8_t  frag[GATT_MTU + 8];         /* a frame that came in more than one ACL packet */
	uint8_t  frag_len, frag_want;
	/* pairing */
	uint8_t  peer[6], peer_type;         /* the phone's address as on the air (LSB first) */
	uint8_t  ltk_rand[8];
	uint16_t ltk_ediv;
	uint8_t  pool[16];                   /* randomness gathered from the radio's events */
	uint32_t rnd_n;
} R BTRAM_BSS;

static uint8_t s_er[16];                 /* the radio's pairing secret (made once, from its own flash) */
static bool    s_er_ok;
static volatile uint8_t s_ltk_due, s_enc_due;
static volatile uint8_t s_upd_due, s_upd_n; /* ask the phone for a slower connection (see BTLE_Tick) */

static uint8_t s_on;                     /* the controller is running */
static uint8_t s_scan, s_adv;            /* what the apps asked for */
static uint8_t s_ser;                    /* the serial link is wanted (BT_Init with a handler) */
static volatile uint8_t s_conn;          /* a phone is connected */
static volatile uint8_t s_adv_again;     /* the link dropped: advertise again (in BTLE_Tick) */
static volatile uint8_t s_credits;       /* ACL packets the controller can still take */
static uint16_t s_handle;
static uint32_t s_credit_ms;             /* when the last packet went out without a credit back */
static BT_ReceiveHandler_t s_rx;
/* The connection rate we ask for (1.25 ms units): 30-50 ms, no latency, 4 s timeout. On Sean's radio (2026-11-27)
 * a phone connected, swapped 7 packets each way, went to 7.5 ms (Android does during service discovery) and the
 * link died 2.4 s later (supervision timeout); the cable monitor went quiet meanwhile, the CPU busy with the
 * radio's interrupts. So the radio asks for a slower rate at once (L2CAP Connection Parameter Update Request). */
#define CONN_MIN 24u
#define CONN_MAX 40u
#define CONN_TIMEOUT 400u
#define CREDITS 6                        /* of config_btctler_le_acl_total_nums (8) */
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

static void in_push(const uint8_t *f, int n)   /* one whole L2CAP frame: length, channel, payload */
{
	if (R.in_n >= 4 || n > (int)sizeof R.in[0]) {
		gBtLe.acl_dropped++;
		return;
	}
	for (int i = 0; i < n; i++)
		R.in[R.iw][i] = f[i];
	R.in_len[R.iw] = (uint8_t)n;
	R.iw = (uint8_t)((R.iw + 1u) & 3u);
	R.in_n++;
}

static void acl_in(const uint8_t *p, int size)  /* an HCI ACL packet: handle and flags, length, data */
{
	int len;

	if (size < 4)
		return;
	len = p[2] | p[3] << 8;
	if (len > size - 4)
		return;
	gBtLe.acl_in++;
	p += 4;
	if ((p[-3] & 0x30u) == 0x10u) {      /* a continuation */
		if (!R.frag_want)
			return;
		for (int i = 0; i < len && R.frag_len < sizeof R.frag; i++)
			R.frag[R.frag_len++] = p[i];
		if (R.frag_len >= R.frag_want) {
			in_push(R.frag, R.frag_want);
			R.frag_want = 0;
		}
		return;
	}
	R.frag_want = 0;
	if (len < 4)
		return;
	int want = 4 + (p[0] | p[1] << 8);
	if (len >= want) {
		in_push(p, want);
	} else if (want <= (int)sizeof R.frag) {
		for (int i = 0; i < len; i++)
			R.frag[i] = p[i];
		R.frag_len = (uint8_t)len;
		R.frag_want = (uint8_t)want;
	} else {
		gBtLe.acl_dropped++;
	}
}

/* the host's receive side, called by the controller's transport (h4_controller_interface: type, packet, size) */
static void stir(const uint8_t *p, int n)     /* event bytes and their timing into the random pool */
{
	uint32_t t = SYSTEM_Ms() ^ R.events * 2654435761u;
	for (int i = 0; i < n; i++)
		R.pool[i & 15] = (uint8_t)((R.pool[i & 15] << 1 | R.pool[i & 15] >> 7) ^ p[i]);
	for (int i = 0; i < 4; i++)
		R.pool[(R.events + i) & 15] ^= (uint8_t)(t >> (8 * i));
}

/* random bytes for pairing: AES under the radio's secret of a counter, the time and the pool */
static void random_bytes(uint8_t *out, int n)
{
	uint8_t in[16], o[16];
	while (n > 0) {
		uint32_t t = SYSTEM_Ms();
		R.rnd_n++;
		for (int i = 0; i < 16; i++)
			in[i] = R.pool[i];
		for (int i = 0; i < 4; i++) {
			in[i] ^= (uint8_t)(R.rnd_n >> (8 * i));
			in[4 + i] ^= (uint8_t)(t >> (8 * i));
		}
		SMP_Aes(s_er, in, o);
		for (int i = 0; i < 16; i++)
			R.pool[i] ^= o[(i + 5) & 15];
		for (int i = 0; i < 16 && n > 0; i++, n--)
			*out++ = o[i];
	}
}

int hci_packet_handler(uint8_t type, const uint8_t *packet, uint16_t size)
{
	R.events++;
	gBtLe.events++;
	stir(packet, size > 16 ? 16 : size);
	if (type == 2) {                     /* ACL data from the phone */
		acl_in(packet, size);
		return 0;
	}
	if (type != 4 || size < 2)
		return 0;
	gBtLe.last_event = packet[0];
	if (packet[0] != 0x13 && !(packet[0] == 0x3E && size >= 3 && packet[2] == 0x02)) {
		uint8_t *l = gBtLe.log[gBtLe.log_n++ & 7u];
		l[0] = (uint8_t)size;
		for (int i = 0; i < 11; i++)
			l[1 + i] = i < size ? packet[i] : 0;
	}
	switch (packet[0]) {
	case 0x3E:                           /* LE Meta */
		if (size < 3)
			break;
		gBtLe.last_sub = packet[2];
		if (packet[2] == 0x02)               /* LE Advertising Report */
			report(packet + 3, size - 3);
		else if (packet[2] == 0x03 && size >= 8 && packet[3] == 0 && (unsigned)(packet[6] | packet[7] << 8) < CONN_MIN)
			s_upd_due = 1;                   /* LE Connection Update Complete, faster than we asked: again */
		else if (packet[2] == 0x01 && size >= 6 && packet[3] == 0) {   /* LE Connection Complete, success */
			s_handle = (uint16_t)((packet[4] | packet[5] << 8) & 0x0FFFu);
			s_credits = CREDITS;
			R.ir = R.iw = R.in_n = 0;
			R.frag_want = 0;
			GATT_Reset("UV5R-MINI", s_rx);
			GATT_Secure(BT_PAIRED_ONLY, false);
			if (size >= 14) {
				R.peer_type = packet[7] & 1u;
				for (int i = 0; i < 6; i++)
					R.peer[i] = packet[8 + i];
			}
			{
				uint8_t ra[6];
				for (int i = 0; i < 6; i++)
					ra[i] = R.addr[5 - i];
				SMP_Reset(R.peer, R.peer_type, ra, 1, s_er, random_bytes);
			}
			s_ltk_due = s_enc_due = 0;
			s_upd_due = 1;
			s_upd_n = 0;
			s_conn = 1;
			gBtLe.conns++;
		} else if (packet[2] == 0x05 && size >= 15) {   /* LE Long Term Key Request: answered in BTLE_Tick */
			for (int i = 0; i < 8; i++)
				R.ltk_rand[i] = packet[5 + i];
			R.ltk_ediv = (uint16_t)(packet[13] | packet[14] << 8);
			s_ltk_due = 1;
		}
		break;
	case 0x08:                           /* Encryption Change */
		if (size >= 6 && packet[2] == 0 && packet[5])
			s_enc_due = 1;
		break;
	case 0x30:                           /* Encryption Key Refresh Complete */
		if (size >= 5 && packet[2] == 0)
			s_enc_due = 1;
		break;
	case 0x05:                           /* Disconnection Complete */
		if (size >= 6 && packet[2] == 0 && s_conn) {
			s_conn = 0;
			s_adv_again = s_ser;
		}
		break;
	case 0x13:                           /* Number Of Completed Packets: credits back */
		for (int i = 0, n = size >= 3 ? packet[2] : 0; i < n && 3 + 4 * i + 4 <= size; i++) {
			const uint8_t *e = packet + 3 + 4 * i;
			if (((e[0] | e[1] << 8) & 0x0FFFu) == s_handle) {
				int c = s_credits + (e[2] | e[3] << 8);
				s_credits = (uint8_t)(c > CREDITS ? CREDITS : c);
			}
		}
		break;
	}
	return 0;
}

/* one L2CAP frame to the phone; false: no room in the controller now */
static bool l2cap_send(uint16_t cid, const uint8_t *data, int len)
{
	static uint8_t pkt[4 + 4 + GATT_MTU + 4];

	if (!s_conn || len > GATT_MTU + 4)
		return false;
	if (!s_credits) {
		/* credits come back with Number Of Completed Packets; if they stop coming, don't hang for ever */
		if ((uint32_t)(SYSTEM_Ms() - s_credit_ms) < 1000u)
			return false;
		s_credits = 1;
	}
	pkt[0] = (uint8_t)s_handle;
	pkt[1] = (uint8_t)(s_handle >> 8);   /* flags 00: first packet, not flushable */
	pkt[2] = (uint8_t)(len + 4);
	pkt[3] = 0;
	pkt[4] = (uint8_t)len;
	pkt[5] = (uint8_t)(len >> 8);
	pkt[6] = (uint8_t)cid;
	pkt[7] = (uint8_t)(cid >> 8);
	for (int i = 0; i < len; i++)
		pkt[8 + i] = data[i];
	s_credits--;
	s_credit_ms = SYSTEM_Ms();
	le_hci_send_acl_packet(pkt, (uint16_t)(8 + len));
	gBtLe.acl_out++;
	return true;
}

static void serve(void)                  /* the frames received: through the GATT server, answers back */
{
	uint8_t out[GATT_MTU + 4];
	uint16_t cid;

	while (R.in_n) {
		const uint8_t *f = R.in[R.ir];
		int len = R.in_len[R.ir] - 4, n;

		if (!s_credits && (uint32_t)(SYSTEM_Ms() - s_credit_ms) < 1000u)
			return;                      /* keep it until the answer can go */
		cid = (uint16_t)(f[2] | f[3] << 8);
		if (cid == L2CAP_CID_SMP)
			n = SMP_Input(f + 4, len, out);  /* pairing (bt/smp.c) */
		else
			n = GATT_Input(cid, f + 4, len, out, &cid);
		R.ir = (uint8_t)((R.ir + 1u) & 3u);
		R.in_n--;
		if (n > 0)
			l2cap_send(cid, out, n);
	}
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

/* The pairing secret ER: AES-CBC-MAC of this radio's own BTIF block and calibration bytes under a fixed key. They
 * differ between radios and can't be read over the air; nothing is written. The same every start, so a paired
 * phone's key is made again (bt/smp.h). */
static void make_er(void)
{
	static const uint8_t K[16] = { 'O', 'P', 'E', 'N', '-', 'U', 'V', '5', 'R', '-', 'P', 'A', 'I', 'R', 0, 1 };
	uint8_t blk[16];

	for (int i = 0; i < 16; i++)
		s_er[i] = 0;
	for (uint32_t a = 0; a < 512u; a += 16u) {   /* BTIF (0xFE000), 512 bytes */
		FLASH_Read(0xFE000u + a, blk, 16);
		for (int i = 0; i < 16; i++)
			blk[i] ^= s_er[i];
		SMP_Aes(K, blk, s_er);
	}
	for (uint16_t a = 0; a < 256u; a += 16u) {    /* calibration */
		for (int i = 0; i < 16; i++)
			blk[i] = (uint8_t)(CALIB_Byte((uint16_t)(a + i)) ^ s_er[i]);
		SMP_Aes(K, blk, s_er);
	}
	s_er_ok = true;
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
	if (!s_er_ok)
		make_er();

	/* the radio block's clocks and supply, as stock's Bluetooth start-up (sub_01E0173E) and our FM start set them:
	 * CLK_CON1 bits 14-15 (Bluetooth clock: PLL 48 MHz), 18-19 (radio ADC: PLL 96 MHz), 20-21 clear; VDC13 1.40 V */
	REG32(0x001E0010u) &= ~(3u << 14 | 3u << 18 | 3u << 20);
	P33_Write(6, (uint8_t)((P33_Read(6) & ~7u) | 7u));

	gBtLe.step = 2;
#if APP_FM_ENABLE
	/* The fast clock, as for the FM radio. At 48 MHz (code fetched from the flash at the same rate) a phone's connection
	 * never got going: the controller took it (LE Connection Complete) and dropped it as "failed to be established"
	 * (0x3E), and no scan response was seen, both answers the radio must send 150 us after a phone's packet (Sean's
	 * radio, 2026-11-27). Test build: whether the fast clock lets the controller's interrupt make that. */
	FMRX_ClockFast();
	bt_pll_para(OSC_HZ, FMRX_SysMHz() * 1000000u, 0, 0);
#else
	bt_pll_para(OSC_HZ, SYS_HZ, 0, 0);
#endif
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
	if (s_conn)
		ll_hci_disconnect(s_handle, 0x13);
	s_conn = 0;
	BTLE_Tick();                         /* let the controller take the commands */
	hci_controller_destory();
	IRQ_Free(38);
	IRQ_Free(39);
#if APP_FM_ENABLE
	FMRX_ClockBack();
#endif
	s_on = 0;
	FMGLUE_BtHeap(false, 0, 0);
	gBtLe.step = 0;
}

static void stop_if_idle(void)
{
	if (!s_scan && !s_adv && !s_ser)
		stop();
}

/* connectable advertising for the serial link: the name, and the serial service in the scan response */
static void adv_serial(void)
{
	static const uint8_t ADV[] = { 2, 0x01, 0x06, 10, 0x09, 'U', 'V', '5', 'R', '-', 'M', 'I', 'N', 'I' };
	static const uint8_t RSP[] = { 17, 0x07, 0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5,
	                               0x01, 0x00, 0x40, 0x6E };

	ll_hci_adv_enable(0);
	/* ADV_IND (0): connectable and scannable, every 100 ms, all three channels */
	ll_hci_adv_set_params(160, 160, 0, 0, (const uint8_t *)"\0\0\0\0\0\0", 7, 0);
	ll_hci_adv_set_data(sizeof ADV, ADV);
	ll_hci_adv_scan_response_set_data(sizeof RSP, RSP);
	ll_hci_adv_enable(1);
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
	if (s_conn && s_ltk_due) {           /* the key for this link: the pairing's, or a paired phone's */
		uint8_t ltk[16];
		s_ltk_due = 0;
		if (SMP_LongTermKey(R.ltk_rand, R.ltk_ediv, ltk))
			ll_hci_long_term_key_request_reply(s_handle, ltk);
		else
			ll_hci_long_term_key_request_nagative_reply(s_handle);
	}
	if (s_conn && s_enc_due) {           /* encrypted: hand out our key if a pairing asked for it */
		uint8_t k1[SMP_MAX], k2[SMP_MAX];
		int l1 = 0, l2 = 0, n;
		s_enc_due = 0;
		n = SMP_Encrypted(k1, &l1, k2, &l2);
		GATT_Secure(BT_PAIRED_ONLY, true);
		if (n >= 1)
			l2cap_send(L2CAP_CID_SMP, k1, l1);
		if (n >= 2)
			l2cap_send(L2CAP_CID_SMP, k2, l2);
	}
	if (s_conn && s_upd_due && s_upd_n < 3u) {
		static const uint8_t REQ[12] = { 0x12, 0, 8, 0, CONN_MIN, 0, CONN_MAX, 0, 0, 0,
		                                 (uint8_t)CONN_TIMEOUT, CONN_TIMEOUT >> 8 };
		uint8_t r[12];
		for (int i = 0; i < 12; i++)
			r[i] = REQ[i];
		r[1] = (uint8_t)(0x40u + s_upd_n);  /* identifier */
		if (l2cap_send(L2CAP_CID_SIG, r, sizeof r)) {
			s_upd_due = 0;
			s_upd_n++;
		}
	}
	if (s_conn)
		serve();
	if (s_adv_again) {
		s_adv_again = 0;
		if (s_ser && !s_conn)
			adv_serial();
	}
}

/* ------------------------------------------------------------------ pairing (bt/bt.h) */
void BT_SetPairable(bool on) { SMP_SetPairable(on); }
bool BT_Pairable(void) { return SMP_Pairable(); }

BT_PairState_t BT_PairState(void)
{
	if (!s_conn)
		return BT_PAIR_NONE;
	switch (SMP_State()) {
	case SMP_PAIRING: return BT_PAIR_PAIRING;
	case SMP_PAIRED:  return SMP_IsEncrypted() ? BT_PAIR_PAIRED : BT_PAIR_PAIRING;
	case SMP_FAILED:  return BT_PAIR_FAILED;
	default:          return BT_PAIR_NONE;
	}
}

int32_t BT_Passkey(void) { return s_conn ? SMP_Passkey() : -1; }

/* ------------------------------------------------------------------ bt/bt.h */
uint8_t     BT_Caps(void) { return BT_CAP_LE_SCAN | BT_CAP_LE_ADV | BT_CAP_LE_DATA; }
const char *BT_Backend(void) { return "JL BLE"; }

bool BT_GetAddress(uint8_t addr[6])
{
	if (!s_on)
		return false;
	for (int i = 0; i < 6; i++)
		addr[i] = R.addr[i];
	return true;
}

/* ------------------------------------------------------------------ the serial link (Nordic UART Service) */
bool BT_IsAvailable(void) { return true; }

void BT_Init(BT_ReceiveHandler_t on_receive)
{
	s_rx = on_receive;
	if (on_receive) {
		if (s_ser || !start())
			return;
		s_ser = 1;
		s_conn = 0;
		adv_serial();
		BTLE_Tick();
		return;
	}
	if (!s_ser)
		return;
	s_ser = 0;
	if (s_on) {
		if (s_conn) {
			ll_hci_disconnect(s_handle, 0x13);   /* remote user terminated connection */
			BTLE_Tick();
		}
		s_conn = 0;
		ll_hci_adv_enable(0);
		BTLE_Tick();
	}
	stop_if_idle();
}

bool BT_IsConnected(void) { return s_conn && GATT_NotifyOn(); }

/* Sends in notifications of up to 20 bytes. Waits (running the controller) while its buffers are full, up to 2 s. */
int BT_Send(const uint8_t *data, uint16_t len)
{
	uint8_t att[GATT_MTU];
	uint32_t t0 = SYSTEM_Ms();

	while (len) {
		int k = len > GATT_CHUNK ? GATT_CHUNK : len;
		if (!s_conn || !GATT_NotifyOn())
			return -1;
		int n = GATT_Notify(data, k, att);
		while (!l2cap_send(L2CAP_CID_ATT, att, n)) {
			if (!s_conn || (uint32_t)(SYSTEM_Ms() - t0) > 2000u)
				return -1;
			BTLE_Tick();
		}
		data += k;
		len = (uint16_t)(len - k);
		BTLE_Tick();
	}
	return 0;
}

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
