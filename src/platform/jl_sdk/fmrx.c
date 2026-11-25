/* Open-UV5R-Mini: FM broadcast receiver. See platform/jl_sdk/fmrx.h. */
#include "platform/jl_sdk/fmrx.h"
#include "bsp/ac695n.h"
#include "driver/io.h"
#include "driver/irq.h"
#include "driver/system.h"
#include "driver/uart.h"

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
#define FM_RATE      37500u
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

/* ------------------------------------------------------------------------------------------- cable log */
/* Everything the FM start finds goes out on the cable UART (CHIRP's, 115200 baud, set up at power-on by
 * program.c), so it can be read on the PC (K1 cable) instead of from photos of the screen (Sean, 2026-11-25). */
static char s_lb[96];
static unsigned s_ln;

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
static volatile bool s_too_fast;
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

/* an exception while FM runs: note where, then wait for the watchdog to restart the radio */
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
		if (gFmExc)                                                                     \
			gFmExc((n), pc, rs);                                                        \
		for (;;)                                                                        \
			if (gFmExc)                                                                 \
				SYSTEM_WatchdogClear();                                                 \
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

/* Called by JieLi's FM interrupt with the demodulated audio: len bytes of interleaved 16-bit left/right at 37.5 kHz
 * (32 frames). Mono, volume, and 37.5 -> 48 kHz by straight-line interpolation. */
__attribute__((section(".ramfunc.fm"))) void fm_sample_output_handler(int16_t *buf, int len)
{
	int16_t out[48];
	uint32_t n = 0, frames = (uint32_t)len / 4u;
	int32_t vol = (int32_t)s_vol * (int32_t)s_vol;          /* 0..225: about a square law, like a volume knob */
	uint16_t peak = 0;

	s_st.irqs++;
	gFmCrumb.irqs++;
	{                                    /* the safety check: 64 interrupts averaging under 600 us = stop */
		uint16_t t = t_now();
		if (s_chk_n)
			s_chk_sum += (uint16_t)(t - s_chk_last);
		s_chk_last = t;
		if (++s_chk_n == 65u) {
			if (s_chk_sum < 64u * 600u * 24u) {
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
			gFmStall(pc, gFmCrumb.irqs, gFmDelays);
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
 * 0x63 (the 480 MHz PLL output / 4 = 120 MHz), SYS_DIV = 3, the flash clock (mask ROM), SYS_DIV = 0x200 (LSB / 3 =
 * 40 MHz), CLK_CON0 = 0x1C2 (back on the PLL). So stock runs FM at 120 MHz, not 96 (our 96 MHz left the main code
 * no time on Sean's radio, 2026-11-25: FM hissed, keys and the power switch went dead). We do the same, but keep the
 * flash below its present speed with the flash divider (/ 3 = 40 MHz) instead of the ROM call. PLL_CON1 0x1E0044
 * bit 24 (480 MHz output on) is set first in case the bootloader left it off. Runs from RAM with interrupts off. */
#define CLK_CON0    REG32(0x001E000Cu)
#define CLK_CON1    REG32(0x001E0010u)
uint32_t gFmClkCon1Was;
#define PLL_CON1_   REG32(0x001E0044u)
__attribute__((section(".ramfunc.fm"), noinline)) static void clock_set(uint32_t div, uint32_t con2)
{
	uint32_t was = IRQ_Save(), con0 = CLK_CON0;

	CLK_CON0 = (con0 & ~(7u << 6)) | 4u << 6;   /* CPU on the crystal */
	__asm__ volatile("csync");
	for (volatile uint32_t i = 0; i < 100u; i++)
		;
	CLK_CON2 = con2;
	__asm__ volatile("csync");
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
	if (((CLK_CON0 >> 6) & 7u) != 7u || (s_old_div & 0xFFu) != 0u)
		return;                          /* not on the PLL as seen on the radio: leave it */
	if (!(PLL_CON1_ & 1u << 24)) {
		PLL_CON1_ |= 1u << 24;
		for (volatile uint32_t i = 0; i < 20000u; i++)
			;
	}
	clock_set((s_old_div & ~(7u << 8 | 7u << 12)) | 2u << 8 | 2u << 12, (s_old_con2 & ~0x3Fu) | 3u | 8u << 2);
	s_fast = true;
}

static void clock_back(void)
{
	if (!s_fast)
		return;
	clock_set(s_old_div, s_old_con2);
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

/* Stock's Bluetooth start-up (sub_01E0173E, run in the emulator, 2026-11-25) sets the wireless block and the radio
 * analog block's first eight registers before FM is ever used; our firmware never did (all read 0 on Sean's
 * radio). Values as stock writes them (facts, from the trace). */
static void wl_base_as_stock(void)
{
	volatile uint32_t *wl = (volatile uint32_t *)0x001F1800u, *ana = (volatile uint32_t *)0x001E1900u;

	*wl = 0x00000080u;
	*wl = 0x000C0080u;
	*wl = 0x000C0081u;
	ana[0] = 0x00448725u;
	ana[1] = 0x0C1C0000u;
	ana[2] = 0x0F717A80u;
	ana[3] = 0x3A3A4B25u;
	ana[4] = 0x00000219u;
	ana[5] = 0x08B60BCEu;
	ana[6] = 0x1FFFF4E0u;
	ana[7] = 0x00000000u;
}

extern void (*gFmIsr)(void);

/* Log the interrupt period, then try the differences from stock one by one until the period is right (700-1000 us)
 * and keep that one; if none is, put everything back (the guard then switches FM off). Sean's log of 2026-11-25:
 * the FM block's buffer fills about 9 times too fast, in proportion to the clock picked in CLK_CON1 bits 22-23
 * (PLL 96 MHz / HSB / LSB), so the hardware's own decimation is not working as on stock. */
static bool rate_fix(void)
{
	volatile uint32_t *con1 = (volatile uint32_t *)0x001E0010u, *adc = (volatile uint32_t *)0x001F1708u;
	volatile uint32_t *wl = (volatile uint32_t *)0x001F1800u, *ana = (volatile uint32_t *)0x001E1900u;
	uint32_t was = IRQ_Save(), c1 = *con1, a = *adc, w = *wl, an[8], tg, p;
	bool ok = false;

	for (unsigned k = 0; k < 8u; k++)
		an[k] = ana[k];
	IRQ_Free(43);
	for (uint32_t c = 0; c < 8u && !ok; c++) {
		*con1 = c1;
		*adc = a;
		*wl = w;
		for (unsigned k = 0; k < 8u; k++)
			ana[k] = an[k];
		if (c & 1u)
			*con1 &= ~(1u << 31);                 /* the bootloader's bit 31 (stock: not known set) */
		if (c & 2u)
			wl_base_as_stock();
		if (c & 4u) {                             /* stock's mode 2 clock: PLL 96 MHz */
			*con1 &= ~(3u << 22);
		}
		for (volatile uint32_t i = 0; i < 50000u; i++)
			;
		SYSTEM_WatchdogClear();
		p = pend_period_us(&tg);
		ls("FMLOG try ");
		ld(c);
		ls(" period_us ");
		ld(p);
		ls(" toggles ");
		ld(tg);
		ls(" con1 ");
		lh(*con1);
		lend();
		ok = p >= 700u && p <= 1000u;
	}
	if (!ok) {
		*con1 = c1;
		*adc = a;
		*wl = w;
		for (unsigned k = 0; k < 8u; k++)
			ana[k] = an[k];
	} else if (gFmIsr) {
		s_too_fast = false;
		s_chk_n = s_chk_sum = 0;
		IRQ_Request(43, 0, gFmIsr);
	}
	ls("FMLOG rate_fix ");
	ls(ok ? "found" : "none");
	lend();
	IRQ_Restore(was);
	return ok;
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
	fm_inside_on();
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
	return fm_inside_freq_set(khz) != 0;
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
	}
}
