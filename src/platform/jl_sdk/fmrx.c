/* Open-UV5R-Mini: FM broadcast receiver. See platform/jl_sdk/fmrx.h. */
#include "platform/jl_sdk/fmrx.h"
#include "bsp/ac695n.h"
#include "driver/io.h"
#include "driver/irq.h"
#include "driver/system.h"

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
static volatile FmrxStats_t s_st;
static uint32_t s_mode;
volatile FmrxCrumb_t gFmCrumb __attribute__((section(".ram2.crumb")));

void (*gFmTrace)(uint32_t step, uint32_t glue);

static void crumb(uint32_t step)
{
	gFmCrumb.step = step;
	__asm__ volatile("csync");
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
		for (;;)                                                                        \
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
static void dac_put(const int16_t *d, uint32_t n)
{
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
void fm_sample_output_handler(int16_t *buf, int len)
{
	int16_t out[48];
	uint32_t n = 0, frames = (uint32_t)len / 4u;
	int32_t vol = (int32_t)s_vol * (int32_t)s_vol;          /* 0..225: about a square law, like a volume knob */
	uint16_t peak = 0;

	s_st.irqs++;
	gFmCrumb.irqs++;
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
	fm_inside_on();
	crumb(80);
	FMRX_Tune(khz);
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
	FMGLUE_HeapReset();
	s_st.step = 0;
	crumb(0);                            /* closed cleanly */
}

bool FMRX_IsOpen(void) { return s_open; }

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
}
