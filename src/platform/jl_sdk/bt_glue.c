/* Open-UV5R-Mini: what JieLi's Bluetooth controller library (btctrler.a) needs from the system (ENABLE_BT_LE).
 *
 * The controller comes from JieLi's public BR23 SDK (fw-AC63_BT_SDK, Apache-2.0; docs/THIRD-PARTY.md), copied into
 * build/sdk/ by tools/sdk/fm_libs.sh; nothing of JieLi's is in this repo. In the SDK it runs as a task of JieLi's
 * OS. Read from the library (its LLVM bitcode, clang -S -emit-llvm), that task is only:
 *
 *     hci_controller_init();
 *     for (;;) { if (os_taskq_pend(0, msg, 14) == OS_TASKQ) dispatch(msg); hci_controller_run_loop(); }
 *
 * and the controller's own work runs in its cooperative "threads" (thread_create / thread_run, inside the library).
 * So no OS is needed: bt_le.c runs the same loop body from our main loop, and this file is the small set of stand-ins
 * around it: the settings the library reads (config_*), a message queue for os_taskq_post_type, a list of software
 * timers run from the main loop, and quiet power, storage and log hooks. Each is written from what the library does
 * with it and JieLi's public headers, not from JieLi's sources.
 *
 * Interrupts, delays, the heap, semaphores, asserts and config_asser are shared with the FM radio's glue (fm_glue.c). */
#include <stddef.h>
#include <stdint.h>
#include "platform/jl_sdk/bt_glue.h"

/* ------------------------------------------------------------------ the settings the controller reads */
/* Our choice: BLE only, scanning and advertising (no connections yet), JieLi's own (non-HCI) host interface as their
 * host uses, the normal mode (not a test mode). Values for the rest: off. */
#define BT_MODULE_LE  2
#define BT_NORMAL     1
#define LE_SLAVE      (1 << 1)
#define LE_ADV        (1 << 2)
#define LE_SCAN       (1 << 3)

const int config_btctler_modules = BT_MODULE_LE;
const int config_btctler_mode = BT_NORMAL;
const int config_btctler_hci_standard = 0;
/* Not const for now: changeable over the cable (!w) before a Bluetooth start, to find which one stops advertising
 * on the radio (2026-11-27: no advertising with these on; it worked with features 0 and roles SCAN | ADV). */
__attribute__((used)) uint64_t config_btctler_le_features = 1;   /* LE_ENCRYPTION: a paired link is encrypted (bt/smp.h) */
__attribute__((used)) int config_btctler_le_roles = LE_SCAN | LE_ADV | LE_SLAVE;   /* slave: the serial link */
const int config_btctler_le_hw_nums = 2;
const int config_btctler_le_rx_nums = 8;
const int config_btctler_le_acl_packet_length = 27;
const int config_btctler_le_acl_total_nums = 8;
const int config_btctler_le_master_multilink = 0;
const int config_btctler_le_slave_multilink = 0;
const int config_btctler_le_slave_conn_update_winden = 500;
const int config_btctler_le_afh_en = 0;
const int config_btctler_single_carrier_en = 1;   /* the SDK's value for BLE-only builds */
uint32_t config_vendor_le_bb = 0;

const int config_btctler_le_tws = 0;
const int config_btctler_bredr_master = 0;
const int config_btctler_dual_a2dp = 0;
const int config_btctler_eir_version_info_len = 0;
const int config_bredr_afh_user = 0;
const int config_bt_function = 0;
const int config_bt_security_vulnerability = 0;
const int config_force_bt_pwr_tab_using_normal_level = 0;
const int config_update_mode = 0;
const int CONFIG_BTCTLER_TWS_ENABLE = 0;
const int CONFIG_BTCTLER_FAST_CONNECT_ENABLE = 0;
const int CONFIG_TWS_POWER_BALANCE_ENABLE = 0;
const int CONFIG_TWS_SUPER_TIMEOUT = 2000;
const int CONFIG_BREDR_INQUIRY = 0;
const int CONFIG_INQUIRY_PAGE_OFFSET_ADJUST = 0;
const int CONFIG_ESCO_MUX_RX_BULK_ENABLE = 0;
const int CONFIG_LMP_NAME_REQ_ENABLE = 0;
const int CONFIG_LMP_PASSKEY_ENABLE = 0;
const int CONFIG_LMP_MASTER_ESCO_ENABLE = 0;
const int CONFIG_WIFI_DETECT_ENABLE = 0;
const int CONFIG_TEST_DUT_CODE = 0;
const int CONFIG_TEST_FCC_CODE = 0;
const int CONFIG_TEST_DUT_ONLY_BOX_CODE = 0;
const int ESCO_FORWARD_ENABLE = 0;
const int ble_disable_wait_enable = 1;
const uint8_t rx_fre_offset_adjust_enable = 1;
const int sniff_support_reset_anchor_point = 0;
const int sniff_long_interval = 800;

/* ------------------------------------------------------------------ the controller task's message queue */
#define Q_LEN   16
#define Q_WORDS 14                       /* as the controller task's own buffer */
#define OS_NO_ERR        0
#define OS_TASKQ         13
#define OS_TASK_NOT_EXIST 14
#define OS_Q_FULL        21
#define OS_Q_EMPTY       22

static int      s_q[Q_LEN][Q_WORDS] BTRAM_BSS;
static uint8_t  s_qr, s_qw, s_qn;
uint32_t        gBtQFull, gBtQPosts;

static bool is_ctrl(const char *name)    /* the controller's task is called "btctrler" */
{
	static const char N[] = "btctrler";
	for (unsigned i = 0; i < sizeof N; i++)
		if (!name || name[i] != N[i])
			return false;
	return true;
}

int os_taskq_post_type(const char *name, int type, int argc, int *argv)
{
	uint32_t w;
	if (!is_ctrl(name))
		return OS_TASK_NOT_EXIST;
	if (argc > Q_WORDS - 1)
		argc = Q_WORDS - 1;
	w = BTGLUE_IrqSave();                /* posts also come from the controller's interrupts */
	if (s_qn >= Q_LEN) {
		BTGLUE_IrqRestore(w);
		gBtQFull++;
		return OS_Q_FULL;
	}
	s_q[s_qw][0] = type;
	for (int i = 0; i < argc; i++)
		s_q[s_qw][1 + i] = argv[i];
	s_qw = (uint8_t)((s_qw + 1u) % Q_LEN);
	s_qn++;
	gBtQPosts++;
	BTGLUE_IrqRestore(w);
	return OS_NO_ERR;
}

int os_taskq_post_msg(const char *name, int argc, ...) { (void)name; (void)argc; return OS_TASK_NOT_EXIST; }

/* the controller task's wait, never blocking: OS_TASKQ with a message, else OS_Q_EMPTY */
int os_taskq_pend(const char *fmt, int *argv, int argc)
{
	uint32_t w;
	(void)fmt;
	w = BTGLUE_IrqSave();
	if (!s_qn) {
		BTGLUE_IrqRestore(w);
		return OS_Q_EMPTY;
	}
	for (int i = 0; i < argc && i < Q_WORDS; i++)
		argv[i] = s_q[s_qr][i];
	s_qr = (uint8_t)((s_qr + 1u) % Q_LEN);
	s_qn--;
	BTGLUE_IrqRestore(w);
	return OS_TASKQ;
}

void BTGLUE_QueueReset(void) { s_qr = s_qw = s_qn = 0; gBtQFull = gBtQPosts = 0; }

/* the SDK starts the controller with task_create(btctrler_task, ...): bt_le.c runs that loop itself instead */
int task_create(void (*task)(void *p), void *p, const char *name) { (void)task; (void)p; (void)name; return 0; }
int task_kill(const char *name) { (void)name; return 0; }
const char *os_current_task(void) { return "btctrler"; }

/* semaphores and mutexes: one task, so a pend never has to wait. fm_glue.c has the rest (os_sem_*, os_mutex_*);
 * btctrler_task_ready, which waits on one, is never called. */
int os_mutex_del(void *m, int o) { (void)m; (void)o; return 0; }

/* ------------------------------------------------------------------ software timers, run from the main loop */
#define TMR_N 12
typedef struct {
	void   (*func)(void *);
	void    *priv;
	uint32_t period;                     /* ms; 0 = free */
	uint32_t due;
	uint8_t  once;
} Tmr_t;
static Tmr_t    s_t[TMR_N] BTRAM_BSS;
static uint32_t s_now;
uint32_t        gBtTimerFull;

static uint16_t tmr_add(void *priv, void (*f)(void *), uint32_t ms, uint8_t once)
{
	for (uint16_t i = 0; i < TMR_N; i++)
		if (!s_t[i].period) {
			s_t[i].func = f;
			s_t[i].priv = priv;
			s_t[i].period = ms ? ms : 1u;
			s_t[i].due = s_now + s_t[i].period;
			s_t[i].once = once;
			return (uint16_t)(i + 1u);   /* 0 means "none" to the library */
		}
	gBtTimerFull++;
	return 0;
}

static Tmr_t *tmr(uint16_t id) { return id && id <= TMR_N ? &s_t[id - 1u] : 0; }

uint16_t sys_timer_add(void *priv, void (*func)(void *), uint32_t msec) { return tmr_add(priv, func, msec, 0); }
uint16_t sys_timeout_add(void *priv, void (*func)(void *), uint32_t msec) { return tmr_add(priv, func, msec, 1); }
uint16_t usr_timer_add(void *priv, void (*func)(void *), uint32_t msec, uint8_t pri) { (void)pri; return tmr_add(priv, func, msec, 0); }
uint16_t usr_timeout_add(void *priv, void (*func)(void *), uint32_t msec, uint8_t pri) { (void)pri; return tmr_add(priv, func, msec, 1); }
void sys_timer_del(uint16_t id) { Tmr_t *t = tmr(id); if (t) t->period = 0; }
void sys_timeout_del(uint16_t id) { sys_timer_del(id); }
void usr_timer_del(uint16_t id) { sys_timer_del(id); }
void usr_timeout_del(uint16_t id) { sys_timer_del(id); }
int  sys_timer_modify(uint16_t id, uint32_t msec)
{
	Tmr_t *t = tmr(id);
	if (!t || !t->period)
		return -1;
	t->period = msec ? msec : 1u;
	t->due = s_now + t->period;
	return 0;
}
int  usr_timer_modify(uint16_t id, uint32_t msec) { return sys_timer_modify(id, msec); }
int  usr_timeout_modify(uint16_t id, uint32_t msec) { return sys_timer_modify(id, msec); }
void sys_timer_re_run(uint16_t id) { Tmr_t *t = tmr(id); if (t && t->period) t->due = s_now + t->period; }
void sys_timer_set_user_data(uint16_t id, void *priv) { Tmr_t *t = tmr(id); if (t) t->priv = priv; }
void *sys_timer_get_user_data(uint16_t id) { Tmr_t *t = tmr(id); return t ? t->priv : 0; }

void BTGLUE_TimersReset(uint32_t now) { for (int i = 0; i < TMR_N; i++) s_t[i].period = 0; s_now = now; gBtTimerFull = 0; }

void BTGLUE_TimersRun(uint32_t now)
{
	s_now = now;
	for (int i = 0; i < TMR_N; i++) {
		Tmr_t *t = &s_t[i];
		if (t->period && (int32_t)(now - t->due) >= 0) {
			void (*f)(void *) = t->func;
			void *p = t->priv;
			if (t->once)
				t->period = 0;
			else
				t->due = now + t->period;
			if (f)
				f(p);
		}
	}
}

/* ------------------------------------------------------------------ power: never sleep */
static uint8_t s_lp_dummy;
void *low_power_get(void *priv, const void *ops) { (void)priv; (void)ops; return &s_lp_dummy; }
void  low_power_put(void *priv) { (void)priv; }
void  low_power_request(void) {}
void  low_power_on(void) {}
int32_t low_power_trace_drift(uint32_t usec) { (void)usec; return 0; }
void  low_power_reset_osc_type(uint8_t type) { (void)type; }
uint8_t low_power_get_default_osc_type(void) { return 0; }
uint8_t low_power_get_osc_type(void) { return 0; }
uint8_t power_is_poweroff_post(void) { return 0; }
void  power_reset_close(void) {}

/* ------------------------------------------------------------------ settings storage: read only, nothing stored */
/* syscfg holds the SDK's Bluetooth name, address and RF trim in the VM area. We never write it (AGENTS.md rule 6),
 * and the controller works without it: bt_le.c sets a random address itself. */
int syscfg_read(uint16_t id, void *buf, uint16_t len) { (void)id; (void)buf; (void)len; return -1; }
int syscfg_write(uint16_t id, void *buf, uint16_t len) { (void)id; (void)buf; (void)len; return -1; }
/* The controller calls through this table (analog.c get_bt_osc_offset_flag: ops->check_id(110), from ll_slave.c and
 * lmp.c), so it must not be NULL: every item reads as not there. Layout: SDK include_lib/system/syscfg_id.h. */
static int cfg_init(void) { return 0; }
static int cfg_check(uint16_t id) { (void)id; return 0; }
static int cfg_rw(uint16_t id, uint8_t *buf, uint16_t len) { (void)id; (void)buf; (void)len; return -1; }
static int cfg_str(uint16_t id, uint8_t *buf, uint16_t len, uint8_t ver) { (void)id; (void)buf; (void)len; (void)ver; return -1; }
static uint8_t *cfg_ptr(uint16_t id, uint16_t *len) { (void)id; if (len) *len = 0; return 0; }
static const struct {
	int (*init)(void);
	int (*check_id)(uint16_t);
	int (*read)(uint16_t, uint8_t *, uint16_t);
	int (*write)(uint16_t, uint8_t *, uint16_t);
	int (*dma_write)(uint16_t, uint8_t *, uint16_t);
	int (*read_string)(uint16_t, uint8_t *, uint16_t, uint8_t);
	uint8_t *(*ptr_read)(uint16_t, uint16_t *);
} s_btif_ops = { cfg_init, cfg_check, cfg_rw, cfg_rw, cfg_rw, cfg_str, cfg_ptr };
const void *get_syscfg_btif_ops(void) { return &s_btif_ops; }
int norflash_ioctl(void *dev, uint32_t cmd, uint32_t arg) { (void)dev; (void)cmd; (void)arg; return -1; }

/* ------------------------------------------------------------------ events for JieLi's application: none here */
void sys_event_notify(void *e) { (void)e; }
void sys_key_event_consume(void *e) { (void)e; }
int  user_send_cmd_prepare(int cmd, int argc, void *argv) { (void)cmd; (void)argc; (void)argv; return -1; }

/* Two symbols that would otherwise pull JieLi's start-up code (startup.S, with its own _start) and settings-flash
 * driver (vm.c, which erases the VM area) out of cpu.a: never wanted here. __BT_UPDATA_JUMP is the jump into
 * JieLi's firmware update over Bluetooth, which this firmware never offers; the flash cache stays as it is. */
void __BT_UPDATA_JUMP(void) {}
void spi_cache_way_switch(uint8_t way) { (void)way; }

/* ------------------------------------------------------------------ logs: every tag off (JieLi's log_print is quiet anyway) */
const uint8_t log_tag_const_c_BL = 0;
const uint8_t log_tag_const_d_Analog = 0;
const uint8_t log_tag_const_d_BL = 0;
const uint8_t log_tag_const_d_LE_BB = 0;
const uint8_t log_tag_const_d_LL = 0;
const uint8_t log_tag_const_d_LL_AFH = 0;
const uint8_t log_tag_const_d_LL_E = 0;
const uint8_t log_tag_const_d_LMP = 0;
const uint8_t log_tag_const_d_TWS = 0;
const uint8_t log_tag_const_d_TWS_LMP = 0;
const uint8_t log_tag_const_e_Analog = 0;
const uint8_t log_tag_const_e_BL = 0;
const uint8_t log_tag_const_e_HCI_LL5 = 0;
const uint8_t log_tag_const_e_HCI_STD = 0;
const uint8_t log_tag_const_d_HCI_STD = 0;
const uint8_t log_tag_const_e_LE5_BB = 0;
const uint8_t log_tag_const_e_LE_BB = 0;
const uint8_t log_tag_const_e_LL = 0;
const uint8_t log_tag_const_e_LL_ADV = 0;
const uint8_t log_tag_const_e_LL_AFH = 0;
const uint8_t log_tag_const_e_LL_E = 0;
const uint8_t log_tag_const_e_LL_EXT_ADV = 0;
const uint8_t log_tag_const_e_LL_EXT_INIT = 0;
const uint8_t log_tag_const_e_LL_EXT_SCAN = 0;
const uint8_t log_tag_const_e_LL_INIT = 0;
const uint8_t log_tag_const_e_LL_M = 0;
const uint8_t log_tag_const_e_LL_PADV = 0;
const uint8_t log_tag_const_e_LL_PHY = 0;
const uint8_t log_tag_const_e_LL_RL = 0;
const uint8_t log_tag_const_e_LL_S = 0;
const uint8_t log_tag_const_e_LL_SCAN = 0;
const uint8_t log_tag_const_e_LL_WL = 0;
const uint8_t log_tag_const_e_LMP = 0;
const uint8_t log_tag_const_i_AES = 0;
const uint8_t log_tag_const_i_Analog = 0;
const uint8_t log_tag_const_i_BL = 0;
const uint8_t log_tag_const_i_HCI_LL = 0;
const uint8_t log_tag_const_i_HCI_LL5 = 0;
const uint8_t log_tag_const_i_HCI_LMP = 0;
const uint8_t log_tag_const_i_HCI_STD = 0;
const uint8_t log_tag_const_i_LE5_BB = 0;
const uint8_t log_tag_const_i_LE_BB = 0;
const uint8_t log_tag_const_i_LL = 0;
const uint8_t log_tag_const_i_LL_ADV = 0;
const uint8_t log_tag_const_i_LL_AFH = 0;
const uint8_t log_tag_const_i_LL_DX = 0;
const uint8_t log_tag_const_i_LL_E = 0;
const uint8_t log_tag_const_i_LL_EXT_ADV = 0;
const uint8_t log_tag_const_i_LL_EXT_INIT = 0;
const uint8_t log_tag_const_i_LL_EXT_SCAN = 0;
const uint8_t log_tag_const_i_LL_INIT = 0;
const uint8_t log_tag_const_i_LL_M = 0;
const uint8_t log_tag_const_i_LL_PADV = 0;
const uint8_t log_tag_const_i_LL_PHY = 0;
const uint8_t log_tag_const_i_LL_RL = 0;
const uint8_t log_tag_const_i_LL_S = 0;
const uint8_t log_tag_const_i_LL_SCAN = 0;
const uint8_t log_tag_const_i_LL_WL = 0;
const uint8_t log_tag_const_i_LMP = 0;
const uint8_t log_tag_const_i_RF = 0;
const uint8_t log_tag_const_i_TWS = 0;
const uint8_t log_tag_const_i_TWS_LMP = 0;
const uint8_t log_tag_const_i_Thread = 0;
const uint8_t log_tag_const_v_LMP = 0;
const uint8_t log_tag_const_w_LL_AFH = 0;

void printf_buf(const void *b, int n) { (void)b; (void)n; }
void put_buf(const void *b, int n) { (void)b; (void)n; }

/* ------------------------------------------------------------------ the C library calls the controller makes */
int memcmp(const void *a, const void *b, size_t n)
{
	const uint8_t *p = a, *q = b;
	for (; n; n--, p++, q++)
		if (*p != *q)
			return *p - *q;
	return 0;
}
size_t strlen(const char *s) { const char *p = s; while (*p) p++; return (size_t)(p - s); }
int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return (uint8_t)*a - (uint8_t)*b;
}
char *strcpy(char *d, const char *s)
{
	char *o = d;
	while ((*d++ = *s++) != 0)
		continue;
	return o;
}

/* ------------------------------------------------------------------ classic Bluetooth and pairing: never used here */
/* The library refers to these from its classic (BR/EDR), TWS and pairing code. Our settings (config_*: BLE only, no
 * connections) never run that code; the stand-ins only let it link, and refuse if ever called. */
void *lbuf_init(void *buf, uint32_t len, int align, int priv) { (void)buf; (void)len; (void)align; (void)priv; return 0; }
void *lbuf_alloc(void *h, uint32_t len) { (void)h; (void)len; return 0; }
void *lbuf_realloc(void *p, int len) { (void)p; (void)len; return 0; }
void  lbuf_free(void *p) { (void)p; }
int   lbuf_free_check(void *p, uint32_t m) { (void)p; (void)m; return 0; }
uint32_t lbuf_free_space(void *h) { (void)h; return 0; }
int   lbuf_avaliable(void *h, int n) { (void)h; (void)n; return 0; }
uint32_t lbuf_real_size(void *p) { (void)p; return 0; }
void  lbuf_state(void *h, void *s) { (void)h; (void)s; }
void  lbuf_dump(void *h) { (void)h; }
int   trim(void *p, int n) { (void)p; (void)n; return 0; }
int   irq_read(uint32_t i) { (void)i; return 0; }
int   bi_initialize(void *a, int n) { (void)a; (void)n; return -1; }
void  bi_terminate(void *a) { (void)a; }
void  bi_free(void *a, void *b) { (void)a; (void)b; }
void *bi_read_from_byte(void *a, const uint8_t *b, int n) { (void)a; (void)b; (void)n; return 0; }
void  bi_wirte_to_byte(void *a, uint8_t *b, int n) { (void)a; (void)b; (void)n; }
void *bi_poly_mul(void *a, void *b, void *c) { (void)a; (void)b; (void)c; return 0; }
void *bi_poly_mod2(void *a, void *b, void *c) { (void)a; (void)b; (void)c; return 0; }
void  f1_function(void) {}
void  f2_function(void) {}
void  f3_function(void) {}
void  g_function(void) {}
int   uECC_compute_public_key(const uint8_t *k, uint8_t *p, const void *c) { (void)k; (void)p; (void)c; return 0; }
int   uECC_shared_secret(const uint8_t *p, const uint8_t *k, uint8_t *s, const void *c) { (void)p; (void)k; (void)s; (void)c; return 0; }
const void *uECC_secp192r1(void) { return 0; }
void  channel_packet_clear(void) {}
int   cheack_tws_connect_state(void) { return 0; }
void  lmp_ch_update_resume_hdl(void) {}
void  tws_host_timeout_set(void) {}
void  tws_host_timeout_del_event(void) {}
