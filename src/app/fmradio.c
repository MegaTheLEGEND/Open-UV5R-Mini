/* Open-UV5R-Mini: FM broadcast radio (ENABLE_APP_FM), 64-108 MHz, on the AC695N's own FM receiver through the
 * speaker (platform/jl_sdk/fmrx.h). Stock has one too (its FM RADIO side-key action); this is ours, on JieLi's FM
 * library from the public SDK.
 *
 * Keys: UP/DOWN 100 kHz; digits type a frequency (4 digits from 100 MHz up, "1015" = 101.5, else 3, "885" = 88.5;
 * MENU takes it early); = searches up to the next station; * and # volume; EXIT goes back. PTT closes it (the BK4819
 * has the speaker again) as for the other apps. The BK4819's receiver is held quiet while FM plays (RADIO_Probe*).
 *
 * It does not start by itself (Sean's first try, 2026-11-24, restarted the radio during the start): the screen first
 * shows how far the last start got (breadcrumbs that survive a restart, fmrx.h), then MENU starts the FM radio, 1 the
 * FM receiver alone (no sound), 2 the DAC alone with a 1 kHz tone.
 *
 * Once running, the three dim lines are (Sean, 2026-11-24): S = how far the start got (9 = all),
 * CK = the clock registers 0x1E0008 / 0x1E000C, H = heap bytes, A = library asserts; IRQ/s should be about 1302 and
 * OUT/s about 48000 when the CPU keeps up. Not tried on a radio yet. */
#include "app/apps.h"
#if APP_FM_ENABLE
#include "app/app.h"
#include "app/keys.h"
#include "platform/jl_sdk/fmrx.h"
#include "radio/radio.h"
#include "radio/rds.h"
#include "ui/screen_apps.h"
#include "driver/system.h"
#include "ui/helper.h"
#include "ui/theme.h"

#define FM_SEEK_STEP 100u                /* kHz */

static UiFm_t   s_f;
static uint32_t s_khz = 100700u;         /* kept while the radio is on; 100.7 has a station where Sean is */
static uint8_t  s_vol = 10;
static uint32_t s_typed_v;
static uint8_t  s_typed;
static bool     s_seek;
static uint32_t s_seek_from;
static uint16_t s_ms, s_msg_ms;
static uint32_t s_last_irqs, s_last_out;
static bool     s_ok, s_started;
static uint32_t s_mode;
static uint8_t  s_rt_len, s_rt_off;      /* RDS radio text: its length, where the scrolling window starts */
static uint16_t s_rt_ms;
static bool     s_rds_view;              /* A/B: all of RDS instead of the FM screen */
static UiRds_t  s_r;

static void draw(bool full);

static char *put_hex(char *p, uint32_t v, int digits)
{
	for (int i = digits - 1; i >= 0; i--) {
		uint32_t d = (v >> (4 * i)) & 0xFu;
		*p++ = (char)(d < 10u ? '0' + d : 'A' + d - 10u);
	}
	*p = 0;
	return p;
}

static void copy(char *d, const char *s, unsigned n)
{
	while (n-- > 1 && *s)
		*d++ = *s++;
	*d = 0;
}

static void freq_text(void)
{
	char *p = APPS_PutInt(s_f.freq, (int)(s_khz / 1000u));
	*p++ = '.';
	*p++ = (char)('0' + s_khz / 100u % 10u);
	*p = 0;
}

static void say(const char *t, bool warn)
{
	copy(s_f.bottom, t, sizeof s_f.bottom);
	s_f.bottom_warn = warn;
	s_msg_ms = 2000;
}

static void hint(void)
{
	copy(s_f.bottom, s_seek ? "SEARCHING..." : "=SEEK */#VOL A/B RDS", sizeof s_f.bottom);
	s_f.bottom_warn = false;
}

static void typed_text(void)
{
	char *p = s_f.bottom;

	copy(p, "TUNE ", sizeof s_f.bottom);
	while (*p)
		p++;
	p = APPS_PutInt(p, (int)s_typed_v);
	*p++ = '_';
	*p = 0;
	s_f.bottom_warn = true;
	s_msg_ms = 0;
}

/* the radio text window: 21 characters of it, scrolling when it is longer */
static void rt_window(void)
{
	const char *rt = RDS_Info()->rt;
	unsigned i;

	if (s_rt_len <= 21u) {
		for (i = 0; i < s_rt_len; i++)
			s_f.rt[i] = rt[i];
		s_f.rt[i] = 0;
		return;
	}
	for (i = 0; i < 21u; i++) {          /* round and round, three spaces between the end and the start */
		unsigned k = (s_rt_off + i) % (s_rt_len + 3u);
		s_f.rt[i] = k < s_rt_len ? rt[k] : ' ';
	}
	s_f.rt[21] = 0;
}

/* RDS changed: the station name (once its PI is known) and the radio text */
static void rds_text(void)
{
	const RdsInfo_t *r = RDS_Info();
	unsigned n = 64;

	if (r->pi)
		copy(s_f.ps, r->ps, sizeof s_f.ps);
	else
		s_f.ps[0] = 0;
	while (n && r->rt[n - 1u] == ' ')
		n--;
	if (n != s_rt_len)
		s_rt_off = 0;
	s_rt_len = (uint8_t)n;
	rt_window();
}

/* "98.9" style: tenths of MHz */
static char *put_mhz(char *p, uint32_t tenths)
{
	p = APPS_PutInt(p, (int)(tenths / 10u));
	*p++ = '.';
	*p++ = (char)('0' + tenths % 10u);
	*p = 0;
	return p;
}

static char *put_str(char *p, const char *t)
{
	while (*t)
		*p++ = *t++;
	*p = 0;
	return p;
}

static char *put_2(char *p, unsigned v)
{
	*p++ = (char)('0' + v / 10u % 10u);
	*p++ = (char)('0' + v % 10u);
	*p = 0;
	return p;
}

/* the RDS page: everything the decoder has */
static void rds_page(bool full)
{
	const RdsInfo_t *r = RDS_Info();
	char *p, cs[5];

	for (unsigned i = 0; i < UI_RDS_LINES; i++)
		s_r.line[i][0] = 0;
	put_mhz(s_r.right, s_khz / 100u);

	p = put_str(s_r.line[0], "PI ");               /* "PI 54A2 KZZU ECC A0" */
	if (r->pi) {
		p = put_hex(p, r->pi, 4);
		RDS_CallSign(r->pi, cs);
		if (cs[0]) {
			*p++ = ' ';
			p = put_str(p, cs);
		}
	} else
		p = put_str(p, "----");
	if (r->ecc) {
		p = put_str(p, " ECC ");
		put_hex(p, r->ecc, 2);
	}
	p = put_str(s_r.line[1], "PS  ");
	put_str(p, r->ps);
	p = put_str(s_r.line[2], "PTY ");
	p = APPS_PutInt(p, r->pty);
	*p++ = ' ';
	put_str(p, RDS_PtyName(r->pty));
	p = put_str(s_r.line[3], "PTYN ");
	put_str(p, r->ptyn);
	p = put_str(s_r.line[4], r->tp ? "TP1 " : "TP0 ");   /* "TP1 TA0 MUSIC DI 0001" */
	p = put_str(p, r->ta ? "TA1 " : "TA0 ");
	p = put_str(p, r->ms ? "MUSIC DI " : "SPEECH DI ");
	for (int b = 3; b >= 0; b--)
		*p++ = (char)('0' + ((r->di >> b) & 1u));
	*p = 0;
	if (r->ct) {                                     /* "UTC 2026-11-27 07:45", "LOCAL 23:45 -8.0H" */
		int32_t m;
		p = put_str(s_r.line[5], "UTC ");
		p = APPS_PutInt(p, r->ct_year);
		*p++ = '-';
		p = put_2(p, r->ct_month);
		*p++ = '-';
		p = put_2(p, r->ct_day);
		*p++ = ' ';
		p = put_2(p, r->ct_hour);
		*p++ = ':';
		put_2(p, r->ct_min);
		m = (int32_t)r->ct_hour * 60 + r->ct_min + r->ct_offset * 30;
		m = (m % 1440 + 1440) % 1440;
		p = put_str(s_r.line[6], "LOCAL ");
		p = put_2(p, (unsigned)(m / 60));
		*p++ = ':';
		p = put_2(p, (unsigned)(m % 60));
		*p++ = ' ';
		*p++ = r->ct_offset < 0 ? '-' : '+';
		{
			unsigned h = (unsigned)(r->ct_offset < 0 ? -r->ct_offset : r->ct_offset);
			p = APPS_PutInt(p, (int)(h / 2u));
			p = put_str(p, h & 1u ? ".5H" : ".0H");
		}
	} else
		put_str(s_r.line[5], "UTC NOT YET (4A)");
	p = put_str(s_r.line[7], "AF");                  /* as many as fit */
	for (unsigned i = 0; i < r->naf && p - s_r.line[7] <= 15; i++) {
		*p++ = ' ';
		p = put_mhz(p, 875u + r->af[i]);
	}
	for (unsigned l = 0; l < 3u; l++) {             /* radio text, 3 lines */
		for (unsigned i = 0; i < 21u; i++)
			s_r.line[8 + l][i] = r->rt[21u * l + i];
		s_r.line[8 + l][21] = 0;
	}
	p = put_str(s_r.line[11], r->sync ? "S " : "- ");   /* "S P 109/199 0 2 4 A" */
	p = put_str(p, r->pilot ? "P " : "- ");
	p = APPS_PutInt(p, (int)(r->blocks_ok > 9999u ? 9999u : r->blocks_ok));
	*p++ = '/';
	p = APPS_PutInt(p, (int)(r->blocks_bad > 9999u ? 9999u : r->blocks_bad));
	for (unsigned t = 0; t < 16u && p - s_r.line[11] < 19; t++)   /* group types seen, hex */
		if (r->group_seen & (1u << t)) {
			*p++ = ' ';
			*p++ = (char)(t < 10u ? '0' + t : 'A' + t - 10u);
		}
	*p = 0;
	UI_RdsDraw(&s_r, full);
}

static void draw(bool full)
{
	if (s_rds_view)
		rds_page(full);
	else
		UI_FmDraw(&s_f, full);
}

static void tune(uint32_t khz)
{
	s_f.ps[0] = s_f.rt[0] = 0;
	s_rt_len = s_rt_off = 0;
	if (khz < FMRX_MIN_KHZ)
		khz = FMRX_MAX_KHZ;
	else if (khz > FMRX_MAX_KHZ)
		khz = FMRX_MIN_KHZ;
	s_khz = khz;
	freq_text();
	draw(false);              /* the new number first: tuning takes a moment */
	(void)FMRX_Tune(khz);
}

/* digits: "1015" -> 101.5 MHz, "885" -> 88.5 MHz */
static void typed_tune(void)
{
	uint32_t v = s_typed_v, khz;

	s_typed = 0;
	if (v >= 640u && v <= 1080u)
		khz = v * 100u;
	else if (v >= 64u && v <= 108u)
		khz = v * 1000u;
	else {
		say("64-108 MHZ ONLY", true);
		return;
	}
	hint();
	tune(khz);
}

static void diag(void)
{
	FmrxStats_t st;
	char *p;
	uint32_t irqs, outs;

	if (!DEBUGGING)                      /* the live counters are for bring-up */
		return;

	FMRX_Stats(&st);
	irqs = st.irqs - s_last_irqs;
	outs = st.dac_out - s_last_out;
	s_last_irqs = st.irqs;
	s_last_out = st.dac_out;

	p = s_f.diag[0];                     /* "S9 CK0200/01C2 T0/40": ADC time-outs / calls */
	*p++ = 'S';
	*p++ = (char)('0' + st.step % 10u);
	*p++ = ' '; *p++ = 'C'; *p++ = 'K';
	p = put_hex(p, st.sys_div, 4);
	*p++ = '/';
	p = put_hex(p, st.clk_con0, 4);
	*p++ = ' '; *p++ = 'T';
	p = APPS_PutInt(p, (int)(st.adc_timeouts > 99u ? 99u : st.adc_timeouts));
	*p++ = '/';
	APPS_PutInt(p, (int)(st.adc_calls > 999u ? 999u : st.adc_calls));

	(void)irqs;
	p = s_f.diag[1];                     /* "IRQ/S 1302 FREE 45%": real rate, CPU left for the rest */
	copy(p, "IRQ/S ", 7);
	p = APPS_PutInt(p + 6, (int)st.irq_hz);
	copy(p, " FREE ", 7);
	p = APPS_PutInt(p + 6, (int)st.main_pct);
	*p++ = '%';
	*p = 0;

	p = s_f.diag[2];                     /* "OUT/S 48000 DR 0 A0" */
	copy(p, "OUT/S ", 7);
	p = APPS_PutInt(p + 6, (int)outs);
	*p++ = ' '; *p++ = 'D'; *p++ = 'R'; *p++ = ' ';
	p = APPS_PutInt(p, (int)(st.dac_drop > 99999u ? 99999u : st.dac_drop));
	*p++ = ' '; *p++ = 'A';
	APPS_PutInt(p, (int)(st.asserts > 99u ? 99u : st.asserts));

	{                                    /* RDS on the first line: "R2D09 P S 453/0" = PI, pilot locked, block sync,
	                                      * good / bad blocks */
		const RdsInfo_t *r = RDS_Info();
		p = s_f.diag[0];
		*p++ = 'R';
		p = put_hex(p, r->pi, 4);
		*p++ = ' '; *p++ = r->pilot ? 'P' : '-'; *p++ = ' '; *p++ = r->sync ? 'S' : '-'; *p++ = ' ';
		p = APPS_PutInt(p, (int)(r->blocks_ok > 99999u ? 99999u : r->blocks_ok));
		*p++ = '/';
		APPS_PutInt(p, (int)(r->blocks_bad > 9999u ? 9999u : r->blocks_bad));
	}
}

/* the breadcrumbs of the last start, before this one: "LAST 57 G4 M3", "IRQS 0 EXC 0", "PC 01E0ABCD 01E0AB00" */
static void last_try(void)
{
	char *p;

	if (gFmCrumb.magic != FMRX_CRUMB_MAGIC) {
		if (DEBUGGING)
			copy(s_f.diag[0], "NO EARLIER TRY", sizeof s_f.diag[0]);
		return;
	}
	if (!DEBUGGING && !gFmCrumb.exc)     /* normal use: shown only after a crash */
		return;
	p = s_f.diag[0];
	copy(p, gFmCrumb.step ? "LAST " : "LAST OK ", 9);
	while (*p)
		p++;
	p = APPS_PutInt(p, (int)gFmCrumb.step);
	*p++ = ' '; *p++ = 'G';
	p = APPS_PutInt(p, (int)gFmCrumb.glue);
	*p++ = ' '; *p++ = 'M';
	APPS_PutInt(p, (int)gFmCrumb.mode);

	p = s_f.diag[1];
	copy(p, "IRQS ", 6);
	p = APPS_PutInt(p + 5, (int)(gFmCrumb.irqs > 999999u ? 999999u : gFmCrumb.irqs));
	*p++ = ' '; *p++ = 'E'; *p++ = 'X'; *p++ = 'C'; *p++ = ' ';
	APPS_PutInt(p, (int)gFmCrumb.exc);

	p = s_f.diag[2];
	*p++ = 'P'; *p++ = 'C'; *p++ = ' ';
	p = put_hex(p, gFmCrumb.exc_pc, 8);
	*p++ = ' ';
	put_hex(p, gFmCrumb.exc_rets, 8);
}

/* slow-motion start: "STEP 71 G1043" on the bottom line, then a short wait (fmrx.h gFmTrace) */
static void trace(uint32_t step, uint32_t glue)
{
	char b[22], *p = b;
	const char *q = "STEP ";

	while (*q)
		*p++ = *q++;
	p = APPS_PutInt(p, (int)step);
	*p++ = ' '; *p++ = 'G';
	APPS_PutInt(p, (int)glue);
	UI_PrintPadded(b, 2, 116, COLOR_WARN, COLOR_BG, 21);
	SYSTEM_DelayMs(400);
}

/* heartbeat, no wait: "A26 T0 D1968 R0ABCDE" = ADC calls, ADC time-outs, delay calls, and the low 6 hex digits of
 * where the last delay call came from (fmrx.h gFmBeat) */
static void beat(void)
{
	char b[24], *p = b;

	*p++ = 'A';
	p = APPS_PutInt(p, (int)(gFmAdcCalls > 999u ? 999u : gFmAdcCalls));
	*p++ = ' '; *p++ = 'T';
	p = APPS_PutInt(p, (int)(gFmAdcTimeouts > 99u ? 99u : gFmAdcTimeouts));
	*p++ = ' '; *p++ = 'D';
	p = APPS_PutInt(p, (int)(gFmDelays > 99999u ? 99999u : gFmDelays));
	*p++ = ' '; *p++ = 'R';
	put_hex(p, gFmDelayFrom, 6);
	UI_PrintPadded(b, 2, 116, COLOR_WARN, COLOR_BG, 21);

	p = b;                               /* "ISR 64 CK0200/01C2": FM interrupts so far, clock registers */
	*p++ = 'I'; *p++ = 'S'; *p++ = 'R'; *p++ = ' ';
	p = APPS_PutInt(p, (int)(gFmCrumb.irqs > 999999u ? 999999u : gFmCrumb.irqs));
	*p++ = ' ';                          /* "120M 00F4C000": CPU MHz, CLK_CON1 as the bootloader left it */
	p = APPS_PutInt(p, (int)FMRX_SysMHz());
	*p++ = 'M'; *p++ = ' ';
	put_hex(p, gFmClkCon1Was, 8);
	UI_PrintPadded(b, 2, 106, COLOR_WARN, COLOR_BG, 21);
}

/* an exception during the start: "EXC 1 PC 01E0ABCD" and "RETS 01E0ABCD" above the heartbeat (fmrx.h gFmExc) */
static void exc_show(uint32_t vec, uint32_t pc, uint32_t rets)
{
	char b[24], *p = b;

	copy(p, "EXC ", 5);
	p += 4;
	p = APPS_PutInt(p, (int)vec);
	copy(p, " PC ", 5);
	put_hex(p + 4, pc, 8);
	UI_PrintPadded(b, 2, 96, COLOR_WARN, COLOR_BG, 21);
	copy(b, "RETS ", 6);
	put_hex(b + 5, rets, 8);
	UI_PrintPadded(b, 2, 106, COLOR_WARN, COLOR_BG, 21);
}

/* the main code stalled during the start: "MAIN 01E0DE7A" and "ISR 1729 D1951", drawn from the FM interrupt (the
 * main code is not drawing then) */
static void stall_show(uint32_t main_pc, uint32_t irqs, uint32_t delays)
{
	char b[24], *p = b;

	copy(p, "MAIN ", 6);
	put_hex(p + 5, main_pc, 8);
	UI_PrintPadded(b, 2, 96, COLOR_WARN, COLOR_BG, 21);
	copy(b, "ISR ", 5);
	p = APPS_PutInt(b + 4, (int)(irqs > 999999u ? 999999u : irqs));
	*p++ = ' '; *p++ = 'D';
	APPS_PutInt(p, (int)(delays > 99999u ? 99999u : delays));
	UI_PrintPadded(b, 2, 106, COLOR_WARN, COLOR_BG, 21);
	p = b;                               /* clock registers: CLK_CON1, CLK_CON2 */
	copy(p, "C1 ", 4);
	p = put_hex(p + 3, *(volatile uint32_t *)0x001E0010u, 8);
	copy(p, " C2 ", 5);
	put_hex(p + 4, *(volatile uint32_t *)0x001E0014u, 5);
	UI_PrintPadded(b, 2, 66, COLOR_WARN, COLOR_BG, 21);
	p = b;                               /* PLL_CON, PLL_CON1 */
	copy(p, "PL ", 4);
	p = put_hex(p + 3, *(volatile uint32_t *)0x001E0040u, 8);
	*p++ = ' ';
	put_hex(p, *(volatile uint32_t *)0x001E0044u, 8);
	UI_PrintPadded(b, 2, 76, COLOR_WARN, COLOR_BG, 21);
	p = b;                               /* loop ticks at the boot clock / at the FM clock, and CLK_CON1 at boot */
	*p++ = 'L';
	p = APPS_PutInt(p, (int)gFmLoop48);
	*p++ = '/';
	p = APPS_PutInt(p, (int)gFmLoopFast);
	*p++ = ' ';
	put_hex(p, gFmClkCon1Was, 8);
	UI_PrintPadded(b, 2, 86, COLOR_WARN, COLOR_BG, 21);
	p = b;                               /* "P853 G700 H40": us between interrupts, demodulator gap, our handler */
	*p++ = 'P';
	p = APPS_PutInt(p, (int)(gFmP_us > 99999u ? 99999u : gFmP_us));
	*p++ = ' '; *p++ = 'G';
	p = APPS_PutInt(p, (int)(gFmG_us > 99999u ? 99999u : gFmG_us));
	*p++ = ' '; *p++ = 'H';
	APPS_PutInt(p, (int)(gFmH_us > 99999u ? 99999u : gFmH_us));
	UI_PrintPadded(b, 2, 116, COLOR_WARN, COLOR_BG, 21);
}

static void start(uint32_t mode)
{
	s_mode = mode;
	s_started = true;
	gFmTrace = DEBUGGING ? trace : 0;     /* the slow-motion step trace and heartbeat: debugging only */
	gFmBeat = DEBUGGING ? beat : 0;
	gFmExc = exc_show;
	gFmStall = stall_show;
	trace(1, 0);
	copy(s_f.bottom, "STARTING...", sizeof s_f.bottom);
	draw(false);
	FMRX_SetVolume(s_vol);
	s_ok = FMRX_Open(s_khz, mode);
	gFmTrace = 0;
	gFmBeat = 0;
	gFmExc = 0;
	gFmStall = 0;
	diag();
	if (!s_ok)
		say(FMRX_TooFast() ? "FM CLOCK WRONG: OFF" : "FM DID NOT START", true);
	else if (!(mode & FMRX_RX))
		copy(s_f.bottom, "1 KHZ TONE */#:VOL", sizeof s_f.bottom);
	else
		hint();
	draw(false);
}

static void level(void)
{
	int r = FMRX_Rssi();
	s_f.level = (uint8_t)(r <= 0 ? 0 : r >= 60 ? 100 : r * 100 / 60);
	s_f.stereo = FMRX_Stereo();
}

static void enter(void)
{
	s_typed = 0;
	s_seek = false;
	s_ms = 0;
	s_msg_ms = 0;
	s_last_irqs = s_last_out = 0;
	s_f = (UiFm_t){ 0 };
	s_f.vol = s_vol;
	s_ok = s_started = false;
	s_rds_view = false;
	freq_text();
	last_try();
	copy(s_f.bottom, "STARTING...", sizeof s_f.bottom);
	s_f.bottom_warn = true;
	draw(true);
	RADIO_ProbeBegin();                  /* the BK4819 quiet: no squelch, no audio */
}

static void leave(void)
{
	FMRX_Close();
	RADIO_ProbeEnd();
}

static AppsResult_t key(KEY_Code_t k)
{
	int d = KEY_Digit(k);

	if (!s_started) {                    /* the start screen */
		if (k == KEY_ACT_EXIT)
			return APPS_EXIT;
		if (k == KEY_ACT_MENU)
			start(FMRX_DAC | FMRX_RX | FMRX_FAST);   /* the receiver needs 96 MHz (fmrx.h) */
		else if (k == KEY_1)
			start(FMRX_RX | FMRX_FAST);
		else if (k == KEY_2)
			start(FMRX_DAC);
		return APPS_STAY;
	}
	if (k == KEY_ACT_AB && (s_mode & FMRX_RX)) {   /* all of RDS, or the FM screen */
		s_rds_view = !s_rds_view;
		draw(true);
		return APPS_STAY;
	}
	if (!(s_mode & FMRX_RX) && d >= 0)
		d = -1;                          /* tone test: no tuning */
	if (d >= 0) {
		s_seek = false;
		s_typed_v = s_typed ? s_typed_v * 10u + (uint32_t)d : (uint32_t)d;
		s_typed++;
		/* 4 digits from 100 MHz up ("1015"), 3 below ("885") */
		if (s_typed >= 4u || (s_typed == 3u && s_typed_v / 100u != 1u))
			typed_tune();
		else
			typed_text();
	} else if (s_typed && k == KEY_ACT_MENU)
		typed_tune();
	else if (s_typed && k == KEY_ACT_EXIT) {
		s_typed = 0;
		hint();
	} else if (k == KEY_ACT_EXIT)
		return APPS_EXIT;
	else if (k == KEY_ACT_UP || k == KEY_ACT_DOWN) {
		s_seek = false;
		tune(k == KEY_ACT_UP ? s_khz + FM_SEEK_STEP : s_khz - FM_SEEK_STEP);
		hint();
	} else if (k == KEY_EQUALS) {
		s_seek = !s_seek;
		s_seek_from = s_khz;
		hint();
	} else if (k == KEY_STAR || k == KEY_HASH) {
		if (k == KEY_STAR && s_vol > 0)
			s_vol--;
		else if (k == KEY_HASH && s_vol < 15)
			s_vol++;
		s_f.vol = s_vol;
		FMRX_SetVolume(s_vol);
	}
	draw(false);
	return APPS_STAY;
}

static AppsResult_t tick(bool full)
{
	if (!s_started) {                    /* the receiver starts by itself on the first tick, after the screen is up
	                                      * (Sean, 2026-11-26: no key press to start it) */
		start(FMRX_DAC | FMRX_RX | FMRX_FAST);
		return APPS_STAY;
	}
	if (s_ok && !(s_mode & FMRX_RX))
		FMRX_ToneFill();
	if (s_ok && s_seek) {                /* one step a tick, stop on a station or after the whole band */
		uint32_t f = s_khz + FM_SEEK_STEP > FMRX_MAX_KHZ ? FMRX_MIN_KHZ : s_khz + FM_SEEK_STEP;
		s_khz = f;
		freq_text();
		if (FMRX_Tune(f) || f == s_seek_from) {
			s_seek = false;
			hint();
		}
		draw(false);
	}
	if (s_msg_ms && (s_msg_ms = s_msg_ms > APP_TICK_MS ? (uint16_t)(s_msg_ms - APP_TICK_MS) : 0) == 0)
		hint();
	if (s_ok && (s_mode & FMRX_RX)) {    /* RDS: decode what the FM interrupt gathered; the text scrolls */
		bool redraw = false;
		if (RDS_Poll()) {
			rds_text();
			redraw = true;
		}
		if (s_rt_len > 21u && (s_rt_ms = (uint16_t)(s_rt_ms + APP_TICK_MS)) >= 400u) {
			s_rt_ms = 0;
			s_rt_off = (uint8_t)((s_rt_off + 1u) % (s_rt_len + 3u));
			rt_window();
			redraw = true;
		}
		if (redraw)
			draw(false);
	}
	if ((s_ms = (uint16_t)(s_ms + APP_TICK_MS)) >= 1000u) {
		s_ms = 0;
		diag();
		if (s_ok && !s_seek)
			level();
		draw(false);
	}
	if (full)
		draw(true);
	return APPS_STAY;
}

const AppDef_t APP_FM = { "FM RADIO", enter, key, tick, leave, false, false };
#endif
