/* Open-UV5R-Mini: FM broadcast receiver. See platform/jl_sdk/fmrx.h. */
#include "platform/jl_sdk/fmrx.h"
#include "app/apps.h"
#include "bsp/ac695n.h"
#include "driver/io.h"
#include "driver/irq.h"
#include "driver/system.h"
#include "driver/uart.h"
#include "radio/rds.h"

static void ls(const char *s);
static void ld(uint32_t v);
static void lend(void);

/* ---- JieLi's FM library (no header ships with it; how each is called is from the library's code and from the
 * stock app's calls to the same library) */
extern void          fm_inside_mem_init(void);
extern void          fm_inside_io_ctrl(int cmd, ...);
extern void          fm_inside_on(void);
extern void          fm_inside_off(void);
extern unsigned char fm_inside_freq_set(unsigned khz);    /* stock: its 10 kHz value times 10 */
extern int           fm_inside_rssi_read(void);
extern int           fm_inside_st_read(void);

/* ---- JieLi's DAC driver */
struct audio_dac_hdl;
struct audio_dac_trim { int16_t left, right, rear_left, rear_right; };
extern int  audio_dac_init(struct audio_dac_hdl *dac, const void *pd);
extern int  audio_dac_do_trim(struct audio_dac_hdl *dac, struct audio_dac_trim *t, uint8_t fast);
extern int  audio_dac_set_trim_value(struct audio_dac_hdl *dac, struct audio_dac_trim *t);
extern int  audio_dac_set_buff(struct audio_dac_hdl *dac, int16_t *buf, int len);
extern int  audio_dac_set_sample_rate(struct audio_dac_hdl *dac, int rate);
extern int  audio_dac_start(struct audio_dac_hdl *dac);
extern int  audio_dac_stop(struct audio_dac_hdl *dac);
extern int  audio_dac_close(struct audio_dac_hdl *dac);
extern int  audio_dac_ch_analog_gain_set(struct audio_dac_hdl *dac, uint8_t ch, uint8_t gain);
extern int  audio_dac_ch_digital_gain_set(struct audio_dac_hdl *dac, uint8_t ch, uint32_t gain);

extern void FMGLUE_HeapReset(void);
extern uint8_t fm_adc_dat[4096];        /* JieLi's FM sample buffer (section .fm_mem, in our .ram2: not cleared) */
extern uint32_t gFmHeapPeak, gFmAsserts, gFmResets, gFmAdcCalls, gFmAdcTimeouts;

/* DAC registers (JieLi's public br23.h: JL_AUDIO at 0x1E2F00, every field a 32-bit slot) */
#define DAC_CON REG32(0x001E2F00u)
#define DAC_ADR REG32(0x001E2F04u)
#define DAC_LEN REG32(0x001E2F08u)
#define DAC_SWP REG32(0x001E2F14u)
#define DAC_SWN REG32(0x001E2F18u)

/* stock's DAC board settings (struct dac_platform_data: two callbacks, then a bit-field word):
 * output 1 (right only), LDO level 2, LDO bias 3, LPF bias 8, LDO load 2, VCMO on */
static const uint32_t DAC_PD[3] = { 0, 0, 0x00068321u };

#define DAC_RATE     48000
#define DAC_POINTS   1024u               /* mono samples in the DAC's ring: 21 ms */
#define FM_RATE      37500u           /* 32 frames every 847-853 us (counted on the radio, 2026-11-26) */
#define STEP_Q16     ((uint32_t)((FM_RATE * 65536ull) / DAC_RATE))   /* 0.78125 */

static uint32_t s_dac_hdl[256] __attribute__((aligned(8)));   /* JieLi's audio_dac_hdl: about 300 bytes */
static int16_t  s_dac_buf[DAC_POINTS] __attribute__((section(".ram2.dac"), aligned(8)));
static struct audio_dac_trim s_trim;
static bool     s_trimmed, s_open;
static uint8_t  s_vol = 10;
static int32_t  s_prev;
static uint32_t s_phase;

/* ------------------------------------------------------------------------------------------- timing */
/* Timer 3 (JieLi's JL_TIMER3, 0x1E0700: CON, CNT, PRD) counting the 24 MHz crystal, wrapping every 65536 counts
 * (2.7 ms): CON bit 0 = count, bit 3 = crystal clock, bit 14 = clear pending (SDK handshake_timer.c). Sean's twelfth
 * try (2026-11-25): FM played (hiss) but keys and the power switch went dead, so how much time the FM interrupt
 * takes is measured, not guessed. */
#define T3_CON REG32(0x001E0700u)
#define T3_CNT REG32(0x001E0704u)
#define T3_PRD REG32(0x001E0708u)
static volatile uint32_t s_per_sum, s_per_n;
static uint16_t s_per_last;
static bool     s_per_ok;
static uint32_t s_loop_base;             /* ticks for the test loop with interrupts off */

static void timer_start(void)
{
	T3_CON = 1u << 14;
	T3_CNT = 0;
	T3_PRD = 0xFFFFu;
	T3_CON = 1u << 0 | 1u << 3;
	s_per_ok = false;
	s_per_sum = s_per_n = 0;
}

static inline uint16_t t_now(void) { return (uint16_t)T3_CNT; }
#define T3_TICKS_PER_MS 2686u            /* measured on Sean's radio against the PC clock (2026-11-26) */

/* ------------------------------------------------------------------------------------------- cable log */
/* Everything the FM start finds goes out on the cable UART (CHIRP's, 115200 baud, set up at power-on by
 * program.c), so it can be read on the PC (K1 cable) instead of from photos of the screen (Sean, 2026-11-25). */
static char s_lb[96];
static unsigned s_ln;
static bool s_log_always;                /* crash lines go out even with DEBUGGING off */

static void lc(char c)
{
	if (s_ln < sizeof s_lb)
		s_lb[s_ln++] = c;
}

static void ls(const char *s)
{
	while (*s)
		lc(*s++);
}

static void lh(uint32_t v)
{
	for (int i = 7; i >= 0; i--) {
		uint32_t d = (v >> (4 * i)) & 0xFu;
		lc((char)(d < 10u ? '0' + d : 'A' + d - 10u));
	}
}

static void ld(uint32_t v)
{
	char t[11];
	int n = 0;

	do {
		t[n++] = (char)('0' + v % 10u);
		v /= 10u;
	} while (v);
	while (n)
		lc(t[--n]);
}

static void lend(void)
{
	lc('\r');
	lc('\n');
	if (DEBUGGING || s_log_always)
		UART_Send((const uint8_t *)s_lb, s_ln);
	s_ln = 0;
}

static void lreg(const char *name, uint32_t addr)
{
	ls(name);
	lc('=');
	lh(*(volatile uint32_t *)(uintptr_t)addr);
	lc(' ');
}

static void log_regs(const char *when)
{
	ls("FMLOG REGS ");
	ls(when);
	lend();
	lreg("SYSDIV", 0x1E0008u); lreg("CON0", 0x1E000Cu); lreg("CON1", 0x1E0010u); lreg("CON2", 0x1E0014u); lend();
	lreg("CON3", 0x1E0018u); lreg("PLL", 0x1E0040u); lreg("PLL1", 0x1E0044u); lreg("PLL2", 0x1E0048u); lend();
	for (uint32_t a = 0x1F1700u; a < 0x1F1720u; a += 16u) {
		lreg("F", a); lreg("", a + 4u); lreg("", a + 8u); lreg("", a + 12u);
		lend();
	}
	for (uint32_t a = 0x1E1900u; a < 0x1E1980u; a += 16u) {
		lreg("A", a); lreg("", a + 4u); lreg("", a + 8u); lreg("", a + 12u);
		lend();
	}
	lreg("WL1800", 0x1F1800u); lreg("IPCON5", 0x10F114u); lreg("IPMASK", 0x10F1A8u); lend();
}

__attribute__((noinline)) static uint32_t test_loop(void)    /* about 0.3 ms at 96 MHz with nothing else */
{
	uint16_t t0 = t_now();

	for (volatile uint32_t i = 0; i < 4000u; i++)
		;
	return (uint16_t)(t_now() - t0);
}
static volatile FmrxStats_t s_st;
static uint32_t s_mode;
volatile FmrxCrumb_t gFmCrumb __attribute__((section(".ram2.crumb")));

void (*gFmTrace)(uint32_t step, uint32_t glue);
void (*gFmBeat)(void);
void (*gFmExc)(uint32_t vec, uint32_t pc, uint32_t rets);
void (*gFmStall)(uint32_t main_pc, uint32_t irqs, uint32_t delays);
static uint32_t s_stall_d, s_stall_n;
static uint32_t s_chk_sum, s_chk_n;
static uint16_t s_chk_last;
static volatile bool s_too_fast, s_in_on;
static uint32_t s_g_sum, s_h_sum, s_gh_n;
static uint16_t s_out_last;
uint32_t gFmP_us, gFmG_us, gFmH_us;
uint32_t gFmLoop48, gFmLoopFast;

static uint32_t s_irq_at_crumb;
static void crumb(uint32_t step)
{
	gFmCrumb.step = step;
	__asm__ volatile("csync");
	ls("FMLOG step ");
	ld(step);
	ls(" irqs ");
	ld(gFmCrumb.irqs);
	ls(" delays ");
	ld(gFmDelays);
	ls(" adc ");
	ld(gFmAdcCalls);
	lend();
	(void)s_irq_at_crumb;
	if (gFmTrace)
		gFmTrace(step, gFmCrumb.glue);
}

static void exc_log(uint32_t n, uint32_t pc, uint32_t rs);
/* an exception while FM runs: note where (screen and cable), show it about 4 s, then let the watchdog restart the
 * radio (it used to keep the watchdog fed for ever, so Sean had to power-cycle it) */
#ifdef __PI32V2__
#define EXC_HANDLER(n)                                                                  \
	static IRQ_HANDLER void exc##n(void)                                                \
	{                                                                                   \
		uint32_t pc, rs;                                                                \
		__asm__ volatile("%0 = reti" : "=r"(pc));                                       \
		__asm__ volatile("%0 = rets" : "=r"(rs));                                       \
		gFmCrumb.exc = (n) + 1u;                                                        \
		gFmCrumb.exc_pc = pc;                                                           \
		gFmCrumb.exc_rets = rs;                                                         \
		exc_log((n), pc, rs);                                                           \
		if (gFmExc)                                                                     \
			gFmExc((n), pc, rs);                                                        \
		for (unsigned i = 0; gFmExc && i < 40u; i++)                                    \
			SYSTEM_DelayMs(100);                 /* the screen shows it about 4 s */     \
		for (;;)                                 /* then the watchdog restarts the radio */ \
			;                                                                           \
	}
#else
#define EXC_HANDLER(n) static void exc##n(void) { gFmCrumb.exc = (n) + 1u; }
#endif
EXC_HANDLER(0)
EXC_HANDLER(1)
EXC_HANDLER(2)

#define DAC ((struct audio_dac_hdl *)s_dac_hdl)

/* write n mono samples into the DAC's ring, as JieLi's DAC-as-IO demo does: SWN is the room left, SWP where to
 * write; writing SWN hands the samples over */
__attribute__((section(".ramfunc.fm"))) static void dac_put(const int16_t *d, uint32_t n)
{
	if (!(s_mode & FMRX_DAC)) {          /* receiver alone: the DAC's buffer address is not ours (Sean's eighth try,
	                                      * 2026-11-25: an exception writing through it, EXC 1 at 0x02001610) */
		s_st.dac_drop += n;
		return;
	}
	while (n) {
		uint32_t room = DAC_SWN & 0xFFFFu, swp = DAC_SWP & 0xFFFFu, len = DAC_LEN & 0xFFFFu, k;
		int16_t *w;

		if (!len || swp >= len)
			break;
		if (swp + room > len)
			room = len - swp;
		k = n < room ? n : room;
		if (!k)
			break;
		w = (int16_t *)(uintptr_t)DAC_ADR + swp;
		for (uint32_t i = 0; i < k; i++)
			w[i] = d[i];
		DAC_SWN = k;
		__asm__ volatile("csync");
		d += k;
		n -= k;
		s_st.dac_out += k;
	}
	s_st.dac_drop += n;
}

static void ls(const char *s);
static void ld(uint32_t v);
static void lh(uint32_t v);
static void lend(void);
static void exc_log(uint32_t n, uint32_t pc, uint32_t rs)
{
	s_log_always = true;
	ls("FMLOG EXC ");
	ld(n);
	ls(" pc ");
	lh(pc);
	ls(" rets ");
	lh(rs);
	ls(" irqs ");
	ld(gFmCrumb.irqs);
	lend();
}

static uint32_t s_isr_log = 99;
static void stall_log(uint32_t pc)
{
	ls("FMLOG stall main ");
	lh(pc);
	ls(" irqs ");
	ld(gFmCrumb.irqs);
	ls(" P ");
	ld(gFmP_us);
	ls(" G ");
	ld(gFmG_us);
	ls(" H ");
	ld(gFmH_us);
	ls(" hf ");
	lh(REG32(0x001F1714u));
	lend();
}

/* Called by JieLi's FM interrupt with the demodulated audio: len bytes of interleaved 16-bit left/right at 41.7 kHz
 * (32 frames). Mono, volume, and 41.7 -> 48 kHz by straight-line interpolation. */
__attribute__((section(".ramfunc.fm"))) void fm_sample_output_handler(int16_t *buf, int len)
{
	int16_t out[48];
	uint32_t n = 0, frames = (uint32_t)len / 4u;
	int32_t vol = (int32_t)s_vol * (int32_t)s_vol;          /* 0..225: about a square law, like a volume knob */
	uint16_t peak = 0;

	s_st.irqs++;
	gFmCrumb.irqs++;
	if (s_isr_log < 3u) {                /* the first calls after the FM block restart, on the cable */
		uint32_t pc = 0;
#ifdef __PI32V2__
		__asm__ volatile("%0 = reti" : "=r"(pc));
#endif
		s_isr_log++;
		ls("FMLOG isr ");
		ld(s_isr_log);
		ls(" main ");
		lh(pc);
		ls(" hf ");
		lh(REG32(0x001F1714u));
		ls(" len ");
		ld((uint32_t)len);
		lend();
	}
	{                                    /* the safety check: 64 interrupts averaging under 600 us = stop. Timer 3
	                                      * counts about 2.69 million a second, not 24 million (counted against the
	                                      * PC clock with the cable monitor, 2026-11-26: 8192 FM interrupts in 6.94 s,
	                                      * 847 us each = 2275 ticks); the 24 MHz figure made the right rate look
	                                      * 8x too fast and this check switched FM off */
		uint16_t t = t_now();
		if (s_chk_n)
			s_chk_sum += (uint16_t)(t - s_chk_last);
		s_chk_last = t;
		if (++s_chk_n == 65u) {
			if (s_in_on || s_chk_sum < 64u * 600u * T3_TICKS_PER_MS / 1000u) {   /* in fm_inside_on: off after 64, as before */
				s_too_fast = true;
				IRQ_Free(43);            /* no more FM interrupts: the main code runs again */
				return;
			}
			s_chk_n = 0;
			s_chk_sum = 0;
		}
	}
	{
		uint32_t pc = 0;
#ifdef __PI32V2__
		__asm__ volatile("%0 = reti" : "=r"(pc));
#endif
		gFmCrumb.exc_rets = pc;          /* where the main code is (shown after a restart) */
		if (gFmDelays != s_stall_d) {
			s_stall_d = gFmDelays;
			s_stall_n = 0;
		} else if (++s_stall_n % 600u == 0u && gFmStall) {
			uint32_t n = s_per_n, gh = s_gh_n;
			gFmP_us = n ? s_per_sum / n / 24u : 0;
			gFmG_us = gh ? s_g_sum / gh / 24u : 0;
			gFmH_us = gh ? s_h_sum / gh / 24u : 0;
			s_per_sum = s_per_n = 0;
			s_g_sum = s_h_sum = s_gh_n = 0;
			stall_log(pc);
			/* no drawing from here any more: the main loop does not call the library's delay(), so this fired every
			 * 0.45 s while FM ran normally, and drawing the screen inside the interrupt can collide with the main
			 * code's own screen transfer and leave it waiting for ever (the freeze suspected 2026-11-26) */
			s_out_last = t_now();        /* the drawing is not counted */
			s_per_ok = false;
			return;
		}
		if (s_per_ok)
			s_g_sum += (uint16_t)(t_now() - s_out_last);
	}
	{
		uint16_t t = t_now();
		if (s_per_ok) {
			s_per_sum += (uint16_t)(t - s_per_last);
			s_per_n++;
		}
		s_per_last = t;
		s_per_ok = true;
	}
	s_st.frames += frames;
	for (uint32_t f = 0; f < frames; f++) {
		int32_t x = ((int32_t)buf[2 * f] + (int32_t)buf[2 * f + 1]) / 2;
		uint16_t a = (uint16_t)(x < 0 ? -x : x);
		if (a > peak)
			peak = a;
		while (s_phase < 65536u) {
			int32_t y = s_prev + (int32_t)(((int64_t)(x - s_prev) * (int32_t)s_phase) >> 16);
			y = y * vol / 160;
			if (y > 32767)
				y = 32767;
			else if (y < -32768)
				y = -32768;
			out[n++] = (int16_t)y;
			s_phase += STEP_Q16;
			if (n == sizeof out / sizeof out[0]) {
				dac_put(out, n);
				n = 0;
			}
		}
		s_phase -= 65536u;
		s_prev = x;
	}
	if (n)
		dac_put(out, n);
	s_st.peak = peak;
	{
		uint16_t t = t_now();
		s_h_sum += (uint16_t)(t - s_per_last);
		s_gh_n++;
		s_out_last = t;
	}
}

static bool dac_open(void)
{
	for (unsigned i = 0; i < sizeof s_dac_hdl / sizeof s_dac_hdl[0]; i++)
		s_dac_hdl[i] = 0;
	for (unsigned i = 0; i < DAC_POINTS; i++)
		s_dac_buf[i] = 0;
	crumb(50);
	audio_dac_init(DAC, DAC_PD);
	s_st.step = 5;
	crumb(51);
	if (!s_trimmed) {                    /* stock keeps its trim in the settings area; we trim once per power-on */
		audio_dac_do_trim(DAC, &s_trim, 0);
		s_trimmed = true;
	}
	crumb(52);
	audio_dac_set_trim_value(DAC, &s_trim);
	s_st.step = 6;
	crumb(53);
	audio_dac_set_buff(DAC, s_dac_buf, (int)sizeof s_dac_buf);
	crumb(54);
	audio_dac_set_sample_rate(DAC, DAC_RATE);
	crumb(55);
	audio_dac_start(DAC);
	crumb(56);
	audio_dac_ch_analog_gain_set(DAC, 0xF, 28);
	audio_dac_ch_digital_gain_set(DAC, 0xF, 16384);
	s_st.step = 7;
	crumb(57);
	return true;
}

/* ------------------------------------------------------------------------------------------- CPU clock */
/* JieLi's clock registers (public BR23 SDK, asm/clock_hw.h): SYS_DIV 0x1E0008 = HSB divider (bits 0-7), LSB divider
 * (8-10), flash (SFC) divider (12-14), each "value + 1"; CLK_CON2 0x1E0014 bits 0-1 = which PLL output the CPU runs
 * from (192, 137, 320, 480 MHz), bits 2-5 = its divider (1, 3, 5, 7 times 1, 2, 4, 8). Sean's radio showed SYS_DIV 0
 * and CLK_CON0 0x01C3 (CPU on the PLL) on 2026-11-24, with the FM interrupt taking nearly all the time. */
#define CLK_SYS_DIV REG32(0x001E0008u)
#define CLK_CON2    REG32(0x001E0014u)
static uint32_t s_old_div, s_old_con2;
static bool     s_fast;

uint32_t FMRX_SysMHz(void)
{
	static const uint16_t pll[4] = { 192, 137, 320, 480 };
	uint32_t c = CLK_CON2, d = (c >> 2) & 0xFu;

	return pll[c & 3u] / ((2u * (d & 3u) + 1u) << (d >> 2)) / ((CLK_SYS_DIV & 0xFFu) + 1u);
}

/* The FM clock, as stock sets it. Stock's own FM open, run in the emulator (emu/fm_trace.py, 2026-11-25), calls
 * clk_set("sys") with: CLK_CON0 0x1E000C = 0x102 (CPU on the 24 MHz crystal while switching), CLK_CON2 = 0x53 then
 * 0x63 (the 480 MHz PLL output / 4 = 120 MHz), SYS_DIV = 3, the flash clock, SYS_DIV = 0x200 (LSB / 3 = 40 MHz),
 * CLK_CON0 = 0x1C2 (back on the PLL). So stock runs FM at 120 MHz.
 * The CPU runs at the flash (SFC) clock: the flash divider (SYS_DIV bits 12-14) divides the CPU too (cable monitor,
 * 2026-11-26: with flash / 3 the core tick timer and a timed loop both showed 40 MHz, with / 2 60, with / 1 120;
 * fm_dem took 1.8 ms at / 3 and 0.4 ms at / 1, against an FM interrupt every 0.85 ms). So, as stock does (stock
 * sub_01E69A4E and its RAM routine 0x02000114), the flash divider goes to / 1 and the flash's own SPI clock is
 * slowed instead: with the cache idle and the flash controller (JL_SFC 0x1F0200) not busy, controller off, BAUD
 * 0x1F0204 = 5 (120 / 6 = 20 MHz on the SPI wires), CON bits 20-23 = 3 (stock: MHz * 30 / 1000), controller on.
 * Both values ran on Sean's radio from the cable monitor (2026-11-26). PLL_CON1 0x1E0044 bit 24 (480 MHz output on)
 * is set first in case the bootloader left it off. Runs from RAM with interrupts off, the CPU on the crystal. */
#define CLK_CON0    REG32(0x001E000Cu)
#define CLK_CON1    REG32(0x001E0010u)
#define SFC_CON     REG32(0x001F0200u)
#define SFC_BAUD    REG32(0x001F0204u)
#define DSP_CON     REG32(0x00101000u)
#define SFC_FAST_BAUD 5u
#define SFC_FAST_RDLY 5u                 /* stock's rule, MHz * 30 / 1000, at 160 MHz */
uint32_t gFmClkCon1Was;
#define PLL_CON1_   REG32(0x001E0044u)
static uint32_t s_old_sfc_con, s_old_sfc_baud;
__attribute__((section(".ramfunc.fm"), noinline)) static void clock_set(uint32_t div, uint32_t con2, uint32_t sfc_con,
                                                                       uint32_t sfc_baud)
{
	uint32_t was = IRQ_Save(), con0 = CLK_CON0;

	CLK_CON0 = (con0 & ~(7u << 6)) | 4u << 6;   /* CPU on the crystal */
	__asm__ volatile("csync");
	for (volatile uint32_t i = 0; i < 100u; i++)
		;
	CLK_CON2 = con2;
	__asm__ volatile("csync");
	while (!(DSP_CON & 0x20u))               /* flash cache idle */
		;
	while (SFC_CON & 0x80000000u)            /* flash controller not busy */
		;
	SFC_CON &= ~1u;
	SFC_BAUD = sfc_baud;
	SFC_CON = sfc_con & ~1u;
	SFC_CON |= sfc_con & 1u;
	CLK_SYS_DIV = div;
	__asm__ volatile("csync");
	for (volatile uint32_t i = 0; i < 100u; i++)
		;
	CLK_CON0 = con0;                         /* back on the PLL */
	__asm__ volatile("csync");
	for (volatile uint32_t i = 0; i < 1000u; i++)
		;
	IRQ_Restore(was);
}

static void clock_fast(void)
{
	s_old_div = CLK_SYS_DIV;
	s_old_con2 = CLK_CON2;
	s_old_sfc_con = SFC_CON;
	s_old_sfc_baud = SFC_BAUD;
	if (((CLK_CON0 >> 6) & 7u) != 7u || (s_old_div & 0xFFu) != 0u)
		return;                          /* not on the PLL as seen on the radio: leave it */
	if (!(PLL_CON1_ & 1u << 24)) {
		PLL_CON1_ |= 1u << 24;
		for (volatile uint32_t i = 0; i < 20000u; i++)
			;
	}
	/* 160 MHz: the 480 MHz PLL output / 3 (CLK_CON2 bits 2-5 = 1); LSB / 4 = 40 MHz as before. 120 MHz (/ 4) left
	 * too little: on Sean's radio (cable monitor, 2026-11-26) fm_dem took 0.645 ms and RDS 0.143 ms of each 0.85 ms
	 * FM block, and the main loop starved until the watchdog restarted the radio; at 160 MHz, 0.497 and 0.106 ms,
	 * and FM ran with about 35% left for the main loop. Stock runs FM at 120 MHz, with no RDS. */
	clock_set((s_old_div & ~(7u << 8 | 7u << 12)) | 3u << 8, (s_old_con2 & ~0x3Fu) | 3u | 1u << 2,
	          (s_old_sfc_con & ~(0xFu << 20)) | SFC_FAST_RDLY << 20, SFC_FAST_BAUD);
	s_fast = true;
}

static void clock_back(void)
{
	if (!s_fast)
		return;
	clock_set(s_old_div, s_old_con2, s_old_sfc_con, s_old_sfc_baud);
	s_fast = false;
}

/* The real FM interrupt period, with the interrupt off: wait for its pending flag (HF_CON1 bit 7, seen set in the
 * log), clear it as JieLi's interrupt does (|= 0x40), 9 times; also counts the half-buffer bit's toggles between
 * two. 853 us wanted. */
static uint32_t pend_period_us(uint32_t *toggles)
{
	volatile uint32_t *hf = (volatile uint32_t *)0x001F1714u;
	uint32_t sum = 0, n = 0, tg = 0, half = *hf & 0x20u;
	uint16_t t0 = t_now(), tl = 0;

	*hf |= 0x40u;
	for (uint32_t guard = 0; guard < 4000000u && n < 9u; guard++) {
		uint32_t v = *hf;
		if ((v & 0x20u) != half) {
			half = v & 0x20u;
			tg++;
		}
		if (v & 0x80u) {
			uint16_t t = t_now();
			*hf |= 0x40u;
			if (n)
				sum += (uint16_t)(t - tl);
			tl = t;
			n++;
		}
		if (n == 0 && (uint16_t)(t_now() - t0) > 60000u)
			break;
	}
	if (toggles)
		*toggles = tg;
	return n > 1 ? sum / (n - 1u) / 24u : 0;
}

extern void (*gFmIsr)(void);

extern void (*gFmIsr)(void);
extern uint8_t fm_adc_dat[];

/* One register bit flipped, the FM interrupt period measured, the register put back. Logged as
 * "FMLOG bit <addr> <bit> <period_us> <toggles>". */

/* The FM block's start, in the order and with the values stock's FM open and JieLi's library write them
 * (emu/fm_trace.py, emu/fm_ours.py), for when a tried bit has stopped it. */
static const uint32_t k_fm_fir[16] = {
	0xFFF4FFFAu, 0x001EFFF9u, 0x00F30073u, 0x01A70171u, 0x00820158u, 0xFEC3FF78u, 0xFFCCFEDCu, 0x01C1010Cu,
	0xFFBB0148u, 0xFD71FE07u, 0x0152FEB5u, 0x03B6039Fu, 0xFC3600D2u, 0xFA22F8E4u, 0x0D84017Eu, 0x21F81A06u,
};
static uint32_t s_sv_adc, s_sv_adc1, s_sv_base;

static void fm_block_restart(void)
{
	REG32(0x001F1700u) = 0x4001u;
	REG32(0x001F1700u) = 0x4009u;
	REG32(0x001F170Cu) = 0x80000000u;
	for (unsigned k = 0; k < 16u; k++)
		REG32(0x001F1718u) = k_fm_fir[k];
	REG32(0x001F1708u) = s_sv_adc & ~1u;
	REG32(0x001F170Cu) = s_sv_adc1;
	REG32(0x001F1704u) = s_sv_base;
	REG32(0x001F1714u) = 0x1u;
	REG32(0x001F1714u) = 0x2001u;
	REG32(0x001F1708u) = s_sv_adc;
	REG32(0x001F1700u) = 0x4019u;
}

/* Debug (2026-11-26): the radio froze as soon as JieLi's FM interrupt (fmhw_isr) ran at 758 us, before it reached
 * fm_sample_output_handler, with no exception logged. This is the same interrupt, step for step as fmhw_isr's code
 * does it (llvm-objdump of the linked library), with a cable line before and after each step for the first calls.
 * fmhw_isr's library variables are static, so their addresses are read from its own instructions. */
extern void fm_dem(void *in, void *state, void *out);
static uint32_t s_fi_base, s_fi_dem, s_fi_out, s_fi_n;

static uint32_t imm32_at(uint32_t a)        /* a 32-bit immediate at a 2-byte aligned address */
{
	return (uint32_t)*(volatile uint16_t *)(uintptr_t)a | (uint32_t)*(volatile uint16_t *)(uintptr_t)(a + 2u) << 16;
}

__attribute__((unused)) static bool fm_isr_parse(uint32_t isr)
{
	/* fmhw_isr: +0x12 "r4 = imm" (C4 FF), +0x1C "r2 = imm" (C2 FF), +0x32 "r2 = imm" (C2 FF) */
	if (*(volatile uint16_t *)(uintptr_t)(isr + 0x12u) != 0xFFC4u ||
	    *(volatile uint16_t *)(uintptr_t)(isr + 0x1Cu) != 0xFFC2u ||
	    *(volatile uint16_t *)(uintptr_t)(isr + 0x32u) != 0xFFC2u)
		return false;
	s_fi_base = imm32_at(isr + 0x14u);
	s_fi_dem = imm32_at(isr + 0x1Eu);
	s_fi_out = imm32_at(isr + 0x34u);
	return true;
}

static void fi_log(const char *what, uint32_t a, uint32_t b)
{
	ls("FMLOG fi ");
	ld(s_fi_n);
	ls(" ");
	ls(what);
	ls(" ");
	lh(a);
	ls(" ");
	lh(b);
	lend();
}

static void canary_log(const char *when);
__attribute__((unused)) static IRQ_HANDLER void fm_isr_ours(void)
{
	uint32_t hf, st, in = 0;
	bool log = s_fi_n < 3u;

	if (log)
		fi_log("enter", REG32(0x001F1714u), 0);
	REG32(0x001F1714u) |= 0x40u;
	hf = REG32(0x001F1714u);
	st = *(volatile uint32_t *)(uintptr_t)(s_fi_base + 20u);
	if (!st) {
		if (log)
			fi_log("no state", hf, 0);
		s_fi_n++;
		return;
	}
	if (*(volatile uint8_t *)(uintptr_t)s_fi_dem == 0) {
		in = *(volatile uint32_t *)(uintptr_t)(s_fi_base + 24u) + ((~hf << 6) & 0x800u);
		if (log)
			fi_log("dem in st", in, st);
		fm_dem((void *)(uintptr_t)in, (void *)(uintptr_t)st, (void *)(uintptr_t)s_fi_out);
		if (log)
			fi_log("dem done", REG32(0x001F1714u), 0);
	} else if (log) {
		fi_log("dem skipped", *(volatile uint8_t *)(uintptr_t)s_fi_dem, 0);
	}
	if (*(volatile uint8_t *)(uintptr_t)(s_fi_base + 28u) == 0) {
		for (uint32_t i = 0; i < 128u; i++)
			((volatile uint8_t *)(uintptr_t)s_fi_out)[i] = 0;
	}
	fm_sample_output_handler((int16_t *)(uintptr_t)s_fi_out, 128);
	if (log) {
		fi_log("out done", 0, 0);
		canary_log("isr");
	}
	s_fi_n++;
}

/* The FM interrupt we run: fmhw_isr's steps (as fm_isr_ours above, without the logging), with RDS first. RDS needs
 * the raw FM samples before fm_dem: read after it (from fm_sample_output_handler) the pilot loop never locked on
 * Sean's radio (2026-11-26, cable log: "pilot 0" on every station; fm_dem appears to work in its input buffer). */
__attribute__((section(".ramfunc.fm"))) static IRQ_HANDLER void fm_isr_rds(void)
{
	uint32_t hf, st, in;

	REG32(0x001F1714u) |= 0x40u;
	hf = REG32(0x001F1714u);
	st = *(volatile uint32_t *)(uintptr_t)(s_fi_base + 20u);
	if (!st)
		return;
	if (*(volatile uint8_t *)(uintptr_t)s_fi_dem == 0) {
		in = *(volatile uint32_t *)(uintptr_t)(s_fi_base + 24u) + ((~hf << 6) & 0x800u);
		RDS_FeBlock((const volatile int32_t *)(uintptr_t)in, 512u);
		fm_dem((void *)(uintptr_t)in, (void *)(uintptr_t)st, (void *)(uintptr_t)s_fi_out);
	}
	if (*(volatile uint8_t *)(uintptr_t)(s_fi_base + 28u) == 0) {
		for (uint32_t i = 0; i < 128u; i++)
			((volatile uint8_t *)(uintptr_t)s_fi_out)[i] = 0;
	}
	fm_sample_output_handler((int16_t *)(uintptr_t)s_fi_out, 128);
}

/* Restart the FM block after JieLi's fm_inside_on, the stock way. Found by a bit scan on Sean's radio
 * (2026-11-25): after fm_inside_on its buffer interrupt came every 94 us (853 thought, 768 by JieLi's 41 667 Hz
 * output rate: 32 frames a call); stopped (0x1F1700 bit 0) and started again in stock's order (fm_block_restart) it
 * came every 758 us. HF_CON1 0x1F1714 bit 13 read 1 before and 0 after. */
/* Debug monitor on the cable (2026-11-26): after fm_inside_on, with the FM interrupt off, the radio waits for
 * commands so the FM block can be tried live from the PC (tools/fmmon.py) without a new build each time. Never
 * writes flash. One command a line, numbers in hex:
 *   r A          read the word at A           w A V   write V to A        o A V / a A V   A |= V / A &= V
 *   d A N        N words from A (max 64)      p       the FM buffer interrupt's period (us) and HF_CON1
 *   k            fill the RAM after the FM buffer with a pattern;  K   count what changed since
 *   s            fill, wait for one FM buffer interrupt, count what changed (how much one block writes)
 *   f            restart the FM block in stock's order (fm_block_restart)
 *   c A [r0 r1 r2 r3]  call the function at A, print r0
 *   x            leave the monitor, carry on with the FM start */
#define FM_MONITOR DEBUGGING

static void canary_fill(void);
static void canary_log(const char *when);

static bool s_mon_go, s_mon_used;

static uint32_t mon_hex(const char **p)
{
	uint32_t v = 0;
	const char *s = *p;

	while (*s == ' ')
		s++;
	for (;; s++) {
		char c = *s;
		if (c >= '0' && c <= '9')
			v = v * 16u + (uint32_t)(c - '0');
		else if (c >= 'a' && c <= 'f')
			v = v * 16u + (uint32_t)(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F')
			v = v * 16u + (uint32_t)(c - 'A' + 10);
		else
			break;
	}
	*p = s;
	return v;
}

static void mon_word(uint32_t a)
{
	ls("MON ");
	lh(a);
	ls(" = ");
	lh(*(volatile uint32_t *)(uintptr_t)a);
	lend();
}

static void mon_cmd(const char *s)
{
	char c = *s++;
	uint32_t a = mon_hex(&s), v = mon_hex(&s);
	volatile uint32_t *r = (volatile uint32_t *)(uintptr_t)(a & ~3u);

	switch (c) {
	case 'r':
		mon_word(a);
		break;
	case 'w':
		*r = v;
		mon_word(a);
		break;
	case 'W': {                          /* W A v0 v1 ...: up to 16 words from A (code or data for the RAM) */
		uint32_t k = 0;
		r[k++] = v;
		while (*s == ' ' && k < 16u)
			r[k++] = mon_hex(&s);
		ls("MON wrote ");
		ld(k);
		ls(" at ");
		lh(a);
		lend();
		break;
	}
	case 'o':
		*r |= v;
		mon_word(a);
		break;
	case 'a':
		*r &= v;
		mon_word(a);
		break;
	case 'd':
		if (v > 64u)
			v = 64u;
		for (uint32_t i = 0; i < v; i += 4u) {
			ls("MON ");
			lh(a + 4u * i);
			ls(":");
			for (uint32_t k = i; k < i + 4u && k < v; k++) {
				ls(" ");
				lh(r[k]);
			}
			lend();
		}
		break;
	case 'p': {
		uint32_t tg, p = pend_period_us(&tg);
		ls("MON period ");
		ld(p);
		ls(" toggles ");
		ld(tg);
		ls(" hf ");
		lh(REG32(0x001F1714u));
		ls(" con ");
		lh(REG32(0x001F1700u));
		ls(" adc ");
		lh(REG32(0x001F1708u));
		lend();
		break;
	}
	case 'k':
		canary_fill();
		ls("MON filled");
		lend();
		break;
	case 'K':
		canary_log("now");
		break;
	case 's': {
		uint32_t g = 0;
		REG32(0x001F1714u) |= 0x40u;
		canary_fill();
		while (!(REG32(0x001F1714u) & 0x80u) && ++g < 4000000u)
			;
		canary_log(g < 4000000u ? "one block" : "no irq");
		break;
	}
	case 'n': {                          /* count a FM buffer interrupts (pending bit), and the timer ticks meanwhile:
	                                      * the PC times it, so both can be checked against a real clock */
		uint32_t cnt = 0, ticks = 0, g = 0;
		uint16_t tl = t_now();
		if (!a)
			a = 1000u;
		REG32(0x001F1714u) |= 0x40u;
		while (cnt < a && g < 400000000u) {
			uint16_t t = t_now();
			ticks += (uint16_t)(t - tl);
			tl = t;
			g++;
			if (REG32(0x001F1714u) & 0x80u) {
				REG32(0x001F1714u) |= 0x40u;
				cnt++;
			}
			if ((g & 0xFFFFFu) == 0u)
				SYSTEM_WatchdogClear();
		}
		ls("MON counted ");
		ld(cnt);
		ls(" ticks ");
		ld(ticks);
		ls(" loops ");
		ld(g);
		lend();
		break;
	}
	case 'b': {                          /* b N: run JieLi's fm_dem N times on the FM buffer (PC times it) */
		uint32_t st, t0 = 0, tk = 0;
		uint16_t tl;
		if (!gFmIsr || !fm_isr_parse((uint32_t)(uintptr_t)gFmIsr)) {
			ls("MON no fmhw_isr");
			lend();
			break;
		}
		st = *(volatile uint32_t *)(uintptr_t)(s_fi_base + 20u);
		tl = t_now();
		for (uint32_t i = 0; i < a; i++) {
			uint16_t t;
			fm_dem((void *)(uintptr_t)s_sv_base, (void *)(uintptr_t)st, (void *)(uintptr_t)s_fi_out);
			t = t_now();
			tk += (uint16_t)(t - tl);
			tl = t;
			SYSTEM_WatchdogClear();
		}
		(void)t0;
		ls("MON dem ");
		ld(a);
		ls(" ticks ");
		ld(tk);
		lend();
		break;
	}
	case 'B': {                          /* B N: an empty counted loop, N times (PC times it): CPU speed */
		for (volatile uint32_t i = 0; i < a; i++)
			;
		ls("MON loop done");
		lend();
		break;
	}
	case 'g':                            /* go: leave the monitor and turn the FM interrupt on as it is now */
		s_mon_go = true;
		ls("MON go");
		lend();
		break;
	case 'f':
		fm_block_restart();
		ls("MON restarted");
		lend();
		break;
	case 'c': {
		uint32_t r2 = mon_hex(&s), r3 = mon_hex(&s);
		uint32_t (*fn)(uint32_t, uint32_t, uint32_t, uint32_t) =
			(uint32_t (*)(uint32_t, uint32_t, uint32_t, uint32_t))(uintptr_t)a;
		uint32_t r0 = 0, r1 = v;
		const char *t = s;
		(void)t;
		/* c A r0 r1 r2 r3: v is r0 here, the rest follow */
		r0 = v;
		r1 = r2;
		r2 = r3;
		r3 = mon_hex(&s);
		ls("MON ret ");
		lh(fn(r0, r1, r2, r3));
		lend();
		break;
	}
	default:
		ls("MON ? r w o a d p k K s f c x");
		lend();
		break;
	}
}

__attribute__((unused)) static void mon_loop(void)
{
	char line[96];
	uint32_t n = 0, idle = 0;

	ls("MON ready (x to go on)");
	lend();
	for (;;) {
		uint8_t b[32];
		int got = UART_Receive(b, (int)sizeof b, 100u);

		SYSTEM_WatchdogClear();
		if (got <= 0) {
			if (++idle >= 50u && !s_mon_used && !FM_DEV_AUTOSTART) {   /* nobody on the cable: on after 5 s */
				ls("MON nobody, going on");
				lend();
				return;
			}
			if (idle >= 100u) {              /* every ~10 s: still here */
				idle = 0;
				ls("MON ready");
				lend();
			}
			continue;
		}
		idle = 0;
		s_mon_used = true;
		for (int i = 0; i < got; i++) {
			char ch = (char)b[i];
			if (ch == '\r' || ch == '\n') {
				line[n] = 0;
				if (n) {
					if (line[0] == 'x') {
						ls("MON bye");
						lend();
						return;
					}
					mon_cmd(line);
					if (s_mon_go)
						return;
				}
				n = 0;
			} else if (n < sizeof line - 1u) {
				line[n++] = ch;
			}
		}
	}
}

/* Debug: does the FM block write past its 4 KB buffer? The RAM after it (to the interrupt table at 0x2BF00) is
 * unused: fill it, and later count the words that changed. Also the 64 bytes before the buffer. */
#define CAN_LO 0x00029820u
#define CAN_HI 0x0002BF00u
static void canary_fill(void)
{
	for (uint32_t a = CAN_LO; a < CAN_HI; a += 4u)
		*(volatile uint32_t *)(uintptr_t)a = 0xA5A5A5A5u;
}

static void canary_log(const char *when)
{
	uint32_t n = 0, lo = 0, hi = 0;

	for (uint32_t a = CAN_LO; a < CAN_HI; a += 4u)
		if (*(volatile uint32_t *)(uintptr_t)a != 0xA5A5A5A5u) {
			if (!n)
				lo = a;
			hi = a;
			n++;
		}
	ls("FMLOG canary ");
	ls(when);
	ls(" changed ");
	ld(n);
	ls(" from ");
	lh(lo);
	ls(" to ");
	lh(hi);
	ls(" vec43 ");
	lh(*(volatile uint32_t *)(uintptr_t)0x0002BFACu);
	lend();
}

/* After fm_inside_on: JieLi's FM interrupt is already right (one 512-sample block every 847 us, 37.5 kHz out;
 * counted against the PC clock 2026-11-26). The earlier "94 us" was timer 3 read as 24 MHz; the FM block restart
 * that "fixed" it set HF_CON1 bits 8-15 (the block length, 64-byte units; JieLi's 32) to 0 = 16 KB a block, which
 * ran over the RAM after the 4 KB buffer and the interrupt table. So: no restart; the cable monitor (a few seconds,
 * only if the PC talks to it), then the interrupt back on. */
static void rate_fix(void)
{
	uint32_t was = IRQ_Save();

	IRQ_Free(43);
	s_sv_adc = REG32(0x001F1708u);
	s_sv_adc1 = REG32(0x001F170Cu);
	s_sv_base = (uint32_t)(uintptr_t)fm_adc_dat;
#if FM_MONITOR
	timer_start();
	mon_loop();
#endif
	s_too_fast = false;
	s_chk_n = s_chk_sum = 0;
	if (gFmIsr) {                        /* ours (RDS before fm_dem) if fmhw_isr's code reads as expected */
		bool ours = fm_isr_parse((uint32_t)(uintptr_t)gFmIsr);
		IRQ_Request(43, 0, ours ? fm_isr_rds : gFmIsr);
		ls(ours ? "FMLOG isr ours (rds)" : "FMLOG isr jieli (no rds)");
		lend();
	}
	IRQ_Restore(was);
	ls("FMLOG irq on, hf ");
	lh(REG32(0x001F1714u));
	lend();
}

bool FMRX_Open(uint32_t khz, uint32_t mode)
{
	if (s_open)
		return true;
	gFmCrumb.magic = FMRX_CRUMB_MAGIC;
	gFmCrumb.irqs = gFmCrumb.exc = gFmCrumb.exc_pc = gFmCrumb.exc_rets = gFmCrumb.glue = 0;
	gFmCrumb.mode = mode;
	s_mode = mode;
	crumb(10);
	s_st = (FmrxStats_t){ 0 };
	s_prev = 0;
	s_phase = 0;
	s_st.sys_div = REG32(0x001E0008u);
	s_st.clk_con0 = REG32(0x001E000Cu);
	FMGLUE_HeapReset();
	for (unsigned i = 0; i < sizeof fm_adc_dat; i++)
		fm_adc_dat[i] = 0;
	IRQ_Init();
	IRQ_SetVector(0, exc0);              /* the core's exceptions go through the same table (JieLi's hwi.h) */
	IRQ_SetVector(1, exc1);
	IRQ_SetVector(2, exc2);
	s_st.step = 1;
	crumb(20);
	if ((mode & FMRX_DAC) && !dac_open())
		return false;
	if (mode & FMRX_DAC)
		IO_Amp(AUDIO_FM, true);
	s_open = true;
	if (!(mode & FMRX_RX)) {
		s_st.step = 9;
		crumb(99);
		return true;
	}
	/* The radio block's clocks, as stock's Bluetooth start-up sets them before FM is ever opened (stock
	 * sub_01E0173E: CLK_CON1 0x1E0010 &= ~0xC000, ~0xC0000, ~0x300000): Bluetooth clock (bits 14-15) from the PLL's
	 * 48 MHz, WL2ADC (18-19, the radio's ADC, which the FM receiver samples with) from the PLL's 96 MHz, bits 20-21
	 * clear (public BR23 SDK, asm/clock_hw.h). We have no Bluetooth start-up, so the FM receiver ran on whatever the
	 * bootloader left there, and the FM block's sampling may have followed the CPU clock (Sean's fourteenth try,
	 * 2026-11-25: at 120 MHz the VCO calibration never locked; at 48 the start stalled once FM ran). */
	gFmClkCon1Was = CLK_CON1;
	CLK_CON1 &= ~(3u << 14 | 3u << 18 | 3u << 20);
	/* The radio block's supply: VDC13 (P33 P3_ANA_CON6 bits 0-2, public BR23 SDK asm/power/p33.h) to 7 = 1.40 V, as
	 * stock's clock start-up does (sub_01E699A6, seen in the emulator); the bootloader leaves 3 = 1.20 V. On Sean's
	 * radio (cable monitor, 2026-11-26) KKZX 98.9 went from RSSI -40 to -32, its pilot from 19 to 24 dB over the
	 * noise, and the FM clicks halved. */
	P33_Write(6, (uint8_t)((P33_Read(6) & ~7u) | 7u));
	/* the faster clock only now: at 96 MHz JieLi's DAC trim did not finish (Sean's ninth try, 2026-11-25: still in
	 * dac_channel_trim after a long wait), at 48 MHz it does */
	{
		timer_start();
		uint32_t was = IRQ_Save();
		gFmLoop48 = test_loop();
		IRQ_Restore(was);
	}
	if (mode & FMRX_FAST)
		clock_fast();
	s_st.sys_div = REG32(0x001E0008u);
	timer_start();
	{
		uint32_t was = IRQ_Save();
		s_loop_base = test_loop();
		gFmLoopFast = s_loop_base;
		IRQ_Restore(was);
	}
	/* as stock's FM open: memory, then four settings (as stock passes them), then the receiver */
	crumb(60);
	fm_inside_mem_init();
	crumb(61);
	fm_inside_io_ctrl(4, -1, -67, 2, 8);
	fm_inside_io_ctrl(5, 8750, 10800, 100);
	fm_inside_io_ctrl(0, 400, 600, 2, 3);
	fm_inside_io_ctrl(1, 34, 34, 1);
	s_st.step = 8;
	crumb(70);
	IRQ_GlobalOn();
	crumb(71);
	s_too_fast = false;
	s_chk_n = s_chk_sum = 0;
	s_in_on = true;                      /* the FM interrupt starved the main code inside fm_inside_on (2026-11-26) */
	fm_inside_on();
	s_in_on = false;
	crumb(80);
	ls("FMLOG too_fast ");
	ld(s_too_fast);
	ls(" loop48 ");
	ld(gFmLoop48);
	ls(" loopfast ");
	ld(gFmLoopFast);
	ls(" con1_boot ");
	lh(gFmClkCon1Was);
	lend();
	log_regs("after fm_inside_on");
	rate_fix();
	log_regs("after rate_fix");
	if (s_too_fast) {
		FMRX_Close();
		return false;
	}
	FMRX_Tune(khz);
	if (s_too_fast) {
		FMRX_Close();
		return false;
	}
	s_st.step = 9;
	crumb(99);
	return true;
}

void FMRX_Close(void)
{
	if (!s_open)
		return;
	s_open = false;
	crumb(100);
	if (s_mode & FMRX_RX)
		fm_inside_off();
	IRQ_GlobalOff();
	IRQ_Init();
	crumb(101);
	if (s_mode & FMRX_DAC) {
		audio_dac_stop(DAC);
		audio_dac_close(DAC);
	}
	IO_Amp(AUDIO_FM, false);
	clock_back();
	FMGLUE_HeapReset();
	s_st.step = 0;
	crumb(0);                            /* closed cleanly */
}

bool FMRX_IsOpen(void) { return s_open; }
bool FMRX_TooFast(void) { return s_too_fast; }

/* DAC test: a 1 kHz tone, 48 samples a cycle, at the volume set, written as the DAC takes it */
void FMRX_ToneFill(void)
{
	static const int16_t SINE[12] = { 0, 8192, 14189, 16384, 14189, 8192, 0, -8192, -14189, -16384, -14189, -8192 };
	static uint8_t ph, sub;
	int16_t b[48];
	int32_t vol = (int32_t)s_vol * (int32_t)s_vol;

	if (!s_open || (s_mode & FMRX_RX) || !(s_mode & FMRX_DAC))
		return;
	for (int k = 0; k < 12; k++) {       /* up to 576 samples (12 ms) a call */
		if ((DAC_SWN & 0xFFFFu) < 48u)
			break;
		for (int i = 0; i < 48; i++) {
			b[i] = (int16_t)(SINE[ph] * vol / 225);
			if (++sub == 4u) {           /* 12 steps of 4 samples: 48 a cycle, 1 kHz at 48 kHz */
				sub = 0;
				ph = (uint8_t)(ph + 1u == 12u ? 0u : ph + 1u);
			}
		}
		dac_put(b, 48);
	}
}

bool FMRX_Tune(uint32_t khz)
{
	if (!s_open || !(s_mode & FMRX_RX) || khz < FMRX_MIN_KHZ || khz > FMRX_MAX_KHZ)
		return false;
	bool found = fm_inside_freq_set(khz) != 0;

	RDS_Reset(0);                        /* a new station: decode from scratch */
	return found;
}

int FMRX_Rssi(void) { return s_open && (s_mode & FMRX_RX) ? fm_inside_rssi_read() : 0; }
bool FMRX_Stereo(void) { return s_open && (s_mode & FMRX_RX) && fm_inside_st_read() != 0; }
void FMRX_SetVolume(uint8_t v) { s_vol = v > 15 ? 15 : v; }

void FMRX_Stats(FmrxStats_t *s)
{
	*s = s_st;
	s->heap_peak = gFmHeapPeak;
	s->asserts = gFmAsserts + gFmResets;
	s->adc_calls = gFmAdcCalls;
	s->adc_timeouts = gFmAdcTimeouts;
	if (s_open && (s_mode & FMRX_RX)) {
		uint32_t sum = s_per_sum, n = s_per_n, t;
		s_per_sum = s_per_n = 0;
		s->irq_hz = sum ? (uint32_t)(24000000ull * n / sum) : 0;
		t = test_loop();
		s->main_pct = t ? s_loop_base * 100u / t : 0;
		ls("FMLOG run irqs ");
		ld(s->irqs);
		ls(" hz ");
		ld(s->irq_hz);
		ls(" main% ");
		ld(s->main_pct);
		ls(" out ");
		ld(s->dac_out);
		ls(" drop ");
		ld(s->dac_drop);
		ls(" peak ");
		ld(s->peak);
		lend();
		{
			const RdsInfo_t *r = RDS_Info();
			ls("FMLOG rds pi ");
			lh(r->pi);
			ls(" pilot ");
			ld(r->pilot);
			ls(" sync ");
			ld(r->sync);
			ls(" ok ");
			ld(r->blocks_ok);
			ls(" bad ");
			ld(r->blocks_bad);
			ls(" ps ");
			ls(r->ps);
			lend();
		}
	}
}
