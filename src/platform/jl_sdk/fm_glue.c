/* Open-UV5R-Mini: what JieLi's FM receiver and audio DAC libraries need from the system (ENABLE_APP_FM).
 *
 * The FM radio links JieLi's own FM receiver (`fm_inside`, `fm_rf`, `fm_scan`, `fm_dem`, in `cpu.a`) and DAC driver
 * (`audio_dac`, in `media.a`) from JieLi's public BR23 SDK (fw-AC63_BT_SDK, Apache-2.0; docs/THIRD-PARTY.md). The
 * libraries are copied out of your SDK clone into build/sdk/ (tools/sdk/fm_libs.sh); nothing of JieLi's is in this
 * repo. They expect JieLi's OS and drivers around them; this file is our own small stand-in for the parts they call:
 *
 *   - memory: a first-fit heap in RAM above our variables (0x22000.., section .ram2: the stock app's RAM, free
 *     under our firmware);
 *   - interrupts: request_irq / local_irq_* on driver/irq.c. The DAC's own interrupt is not turned on: we feed the
 *     DAC by writing its buffer ourselves (platform/jl_sdk/fmrx.c);
 *   - the SAR ADC sample start JieLi's FM calibration uses (it then reads the ADC registers itself);
 *   - delays, and quiet log / printf / assert hooks;
 *   - OS calls (semaphores, mutexes, timeouts, audio streams) the DAC driver makes but our use never needs.
 * Each stand-in is written from what the library does with it (its code, read with llvm-objdump) and JieLi's
 * public headers, not copied from JieLi's sources. */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "bsp/ac695n.h"
#include "driver/irq.h"
#include "driver/system.h"
#include "platform/jl_sdk/fmrx.h"

/* the last call JieLi's code made into this file, for the breadcrumbs (fmrx.h) */
#define GLUE(id) (gFmCrumb.glue = (id))
/* the rarer calls are also shown on the screen during a slow-motion start (fmrx.h) */
static void glue_show(uint32_t id)
{
	gFmCrumb.glue = id;
	if (gFmTrace)
		gFmTrace(gFmCrumb.step, id);
}

/* ------------------------------------------------------------------------------------------- memory */
#define HEAP_SIZE 0x6000u                /* 24 KB: the FM receiver takes about 10.2 KB, the DAC driver a little */

/* Two heaps with one malloc: the FM heap (FM receiver, DAC driver: also the key beeps, open all the time) and,
 * while Bluetooth runs, the Bluetooth heap in btram (FMGLUE_BtHeap, bt_le.c). malloc takes from the Bluetooth heap
 * while it is on; free gives a block back to the heap it came from, found by its address. */
typedef struct Blk { uint32_t size; uint32_t used; } Blk_t;   /* size: bytes after the header */
typedef struct { uint8_t *base; uint32_t size; } Heap_t;
static uint8_t s_heap[HEAP_SIZE] __attribute__((section(".ram2.heap"), aligned(8)));
static uint8_t s_heap_ready;
static Heap_t  s_fm = { s_heap, HEAP_SIZE }, s_bt;
uint32_t gFmHeapUsed, gFmHeapPeak, gFmAllocFails, gBtHeapUsed, gBtHeapPeak, gBtAllocFails;

static void heap_init_at(const Heap_t *h)
{
	Blk_t *b = (Blk_t *)h->base;
	b->size = h->size - sizeof(Blk_t);
	b->used = 0;
}

static void heap_init(void)
{
	heap_init_at(&s_fm);
	s_heap_ready = 1;
	gFmHeapUsed = 0;
}

/* everything allocated is given back at once (FMRX_Close: the libraries keep pointers we can't track) */
uint32_t gFmAdcCalls, gFmAdcTimeouts, gFmDelays, gFmDelayFrom;

void FMGLUE_HeapReset(void)              /* at every FM start */
{
	heap_init();
	gFmAdcCalls = gFmAdcTimeouts = gFmDelays = 0;
}

void FMGLUE_BtHeap(bool on, void *base, uint32_t size)
{
	if (on) {
		s_bt.base = (uint8_t *)(((uintptr_t)base + 7u) & ~(uintptr_t)7u);
		s_bt.size = (size - (uint32_t)(s_bt.base - (uint8_t *)base)) & ~7u;
		heap_init_at(&s_bt);
		gBtHeapUsed = gBtAllocFails = 0;
	} else
		s_bt.base = 0;
}

static void *heap_alloc(const Heap_t *h, size_t n, uint32_t *used, uint32_t *peak, uint32_t *fails)
{
	uint8_t *p = h->base;

	n = (n + 7u) & ~(size_t)7u;
	if (!n)
		n = 8;
	while (p < h->base + h->size) {
		Blk_t *b = (Blk_t *)p;
		if (!b->used && b->size >= n) {
			if (b->size >= n + sizeof(Blk_t) + 16u) {      /* split */
				Blk_t *r = (Blk_t *)(p + sizeof(Blk_t) + n);
				r->size = b->size - (uint32_t)n - sizeof(Blk_t);
				r->used = 0;
				b->size = (uint32_t)n;
			}
			b->used = 1;
			*used += b->size + sizeof(Blk_t);
			if (*used > *peak)
				*peak = *used;
			return p + sizeof(Blk_t);
		}
		p += sizeof(Blk_t) + b->size;
	}
	(*fails)++;
	return 0;
}

void *malloc(size_t n)
{
	GLUE(1);
	if (s_bt.base)
		return heap_alloc(&s_bt, n, &gBtHeapUsed, &gBtHeapPeak, &gBtAllocFails);
	if (!s_heap_ready)
		heap_init();
	return heap_alloc(&s_fm, n, &gFmHeapUsed, &gFmHeapPeak, &gFmAllocFails);
}

static bool heap_free(const Heap_t *h, void *q, uint32_t *used)
{
	Blk_t *b, *n;

	if (!h->base || (uint8_t *)q < h->base + sizeof(Blk_t) || (uint8_t *)q >= h->base + h->size)
		return false;
	b = (Blk_t *)((uint8_t *)q - sizeof(Blk_t));
	if (!b->used)
		return true;
	b->used = 0;
	*used -= b->size + sizeof(Blk_t);
	n = (Blk_t *)((uint8_t *)b + sizeof(Blk_t) + b->size);         /* join with a free block after it */
	if ((uint8_t *)n < h->base + h->size && !n->used)
		b->size += sizeof(Blk_t) + n->size;
	return true;
}

void free(void *q)
{
	if (q && !heap_free(&s_bt, q, &gBtHeapUsed))
		heap_free(&s_fm, q, &gFmHeapUsed);
}

void *zalloc(size_t n)
{
	uint8_t *p = malloc(n);
	if (p)
		for (size_t i = 0; i < n; i++)
			p[i] = 0;
	return p;
}

void *calloc(size_t a, size_t b) { return zalloc(a * b); }

/* ------------------------------------------------------------------------------------------- interrupts */
static uint32_t s_irq_was;
static volatile int s_irq_depth;
void (*gFmDacIrqHandler)(void);          /* the DAC driver's handler: kept, not turned on */

void local_irq_disable(void)
{
	uint32_t w = IRQ_Save();
	if (s_irq_depth++ == 0)
		s_irq_was = w;
}

void local_irq_enable(void)
{
	if (s_irq_depth > 0 && --s_irq_depth == 0)
		IRQ_Restore(s_irq_was);
}

void __local_irq_enable(void) { local_irq_enable(); }

void (*gFmIsr)(void);

void request_irq(uint8_t n, uint8_t priority, void (*h)(void), uint8_t cpu)
{
	(void)cpu;
	glue_show(1000u + n);
	if (n == IRQ_AUDIO) {
		gFmDacIrqHandler = h;
		return;
	}
	if (n == 43u)
		gFmIsr = h;                      /* JieLi's FM interrupt, kept for fmrx.c's rate check */
	IRQ_Request(n, priority, h);
}

void unrequest_irq(uint8_t n)
{
	if (n != IRQ_AUDIO)
		IRQ_Free(n);
}

void bit_set_ie(uint8_t n) { (void)n; }
void bit_clr_ie(uint8_t n) { (void)n; }

/* ------------------------------------------------------------------------------------------- SAR ADC */
/* JieLi's FM calibration measures its LDO and the band-gap: it calls adc_sample(channel) to start a conversion,
 * then waits for the done bit (CON bit 7) and reads RES itself. Channel 15 is the analog mux; the upper bits say
 * which block (PMU 0, audio 1, PLL 2, FM 3, BT 4, bits 16-19) and which signal in it (bits 20+), as JieLi's
 * public adc_api.h. Our battery reading (driver/adc.c) sets the ADC up again on each read, so sharing is fine. */
#define ADC_CON     REG32(0x001E3100u)
#define ADC_RES     REG32(0x001E3104u)
#define PLL_CON1    REG32(0x001E0044u)
#define WLA_CON4    REG32(0x001E1910u)
#define WLA_CON25   REG32(0x001E1964u)
#define ADA_CON3    REG32(0x001E19ECu)

extern void adc_pmu_detect_en(uint32_t on);   /* JieLi cpu.a (P33) */
extern void adc_pmu_ch_select(uint32_t ch);

static void adc_mux(uint32_t ch)
{
	uint32_t blk = (ch >> 16) & 0xFu, sig = ch >> 20;

	adc_pmu_detect_en(0);                                     /* every mux source off first */
	ADA_CON3 = (ADA_CON3 & ~(0x7Fu << 24)) | (0x73u << 24);
	PLL_CON1 &= ~(1u << 18);
	WLA_CON25 &= ~(1u << 19);
	WLA_CON4 &= ~(1u << 6);
	switch (blk) {
	case 0:                                                   /* PMU */
		adc_pmu_detect_en(1);
		adc_pmu_ch_select(sig);
		break;
	case 1: {                                                 /* audio */
		uint32_t t = 0x73u;
		t = (sig == 2 || sig == 3) ? (t | 1u << sig) : (t & ~(1u << sig));
		ADA_CON3 = (ADA_CON3 & ~(0x7Fu << 24)) | (t & 0x7Fu) << 24;
		break;
	}
	case 2:                                                   /* PLL */
		PLL_CON1 = (PLL_CON1 | 1u << 18) & ~(3u << 16);
		PLL_CON1 |= (sig & 3u) << 16;
		break;
	case 3:                                                   /* FM */
		WLA_CON25 = (WLA_CON25 | 1u << 19) & ~(7u << 21);
		WLA_CON25 |= (sig & 7u) << 21;
		break;
	case 4:                                                   /* BT */
		WLA_CON4 |= 1u << 6;
		break;
	default:
		break;
	}
}

/* The library polls the done bit with no time limit and interrupts off, so a conversion that never ends hangs the
 * start until the watchdog restarts the radio (Sean's third try, 2026-11-24: the last step shown was "71 G3", the
 * first ADC call of the FM calibration). So we wait for it here with a limit first, and show each part on the
 * screen during a slow-motion start: G3n0 entry, G3n1 mux set, G3n2 started, G3n3 done, G3n4 timed out (n = call
 * number). Only the first two calls are shown, and every time-out: the calibration makes dozens of calls (Sean's
 * fourth try, 2026-11-24: G3091-G3093 went round and round, 1.2 s each). After a time-out we start a conversion on the battery channel (6, known to work), so the library's
 * own wait ends; its calibration value is then wrong, but the start goes on and the screen says how far. */
static void adc_show(uint32_t id)
{
	if (gFmAdcCalls <= 2u) {
		glue_show(id);
	} else {
		GLUE(id);
		if (gFmBeat && (id % 10u) == 0u)
			gFmBeat();
	}
}

uint32_t adc_sample(uint32_t ch)
{
	uint32_t con = 0x6u | 0xF000u | 1u << 3 | 1u << 6 | (ch & 0xFu) << 8;   /* clock /96, start delay, no irq */
	uint32_t id = 3000u + (++gFmAdcCalls < 9u ? gFmAdcCalls : 9u) * 10u;
	uint32_t n;

	adc_show(id);
	if ((ch & 0xFFFFu) == 0xFu)
		adc_mux(ch);
	adc_show(id + 1u);
	ADC_CON = con;
	ADC_CON |= 1u << 4;                                       /* on */
	ADC_CON |= 1u << 6;                                       /* start */
	adc_show(id + 2u);
	for (n = 0; n < 200000u && !(ADC_CON & 0x80u); n++)
		;
	if (ADC_CON & 0x80u) {
		adc_show(id + 3u);
		return 0;
	}
	gFmAdcTimeouts++;
	glue_show(id + 4u);
	ADC_CON = 0xF04Eu | 6u << 8;                              /* battery channel, as driver/adc.c */
	ADC_CON |= 0x10u;
	ADC_CON |= 0x40u;
	return 0;
}

/* JieLi's conversion: the band-gap is 1168 mV, trimmed by 3.2 mV steps from the efuse (get_vbg_trim, cpu.a) */
extern uint16_t get_vbg_trim(void);
uint32_t adc_value_to_voltage(uint32_t adc_vbg, uint32_t adc_ch_val)
{
	uint32_t t = get_vbg_trim(), step = t & 0x1Fu;
	uint32_t ref10 = (t & 0x20u) ? 11680u - step * 32u : 11680u + step * 32u;   /* tenths of a mV */

	if (!adc_vbg)
		return 0;
	return adc_ch_val * ref10 / 10u / adc_vbg;
}

/* ------------------------------------------------------------------------------------------- delays */
void delay(volatile uint32_t n)          /* JieLi's busy loop: a count, not a time */
{
	/* the watchdog fed on every call: JieLi's DAC trim makes up to about 22 000 calls of delay(10) in a row
	 * (dac_channel_trim's last search, 2048 steps of 11), seconds with no longer delay, and the radio restarted in
	 * it (Sean's eleventh try, 2026-11-25: LAST 51 G4) */
	GLUE(4);
	SYSTEM_WatchdogClear();
	gFmDelays++;
	gFmDelayFrom = (uint32_t)(uintptr_t)__builtin_return_address(0);
	/* the screen line costs far more than a short delay: only every 1024th call. Every call from step 71 on made
	 * fm_inside_on take about 19 s (Sean's radio, cable log 2026-11-26: about 2000 delays, 4000 screen lines); the
	 * DAC trim's thousands of delays once took minutes (Sean's tenth try, 2026-11-25) */
	if (gFmBeat && (gFmDelays & 1023u) == 0u)
		gFmBeat();
	while (n--)
		__asm__ volatile("nop");
}

void delay_2ms(uint32_t n) { GLUE(5); SYSTEM_DelayMs(2u * n); }
void udelay(uint32_t us) { SYSTEM_DelayUs(us); }
void mdelay(uint32_t ms) { SYSTEM_DelayMs(ms); }
void os_time_dly(uint32_t ticks) { GLUE(6); SYSTEM_DelayMs(10u * ticks); }

/* ------------------------------------------------------------------------------------------- logs, asserts */
/* JieLi's failed checks: 0 = their own chip reset in some libraries (btctrler.a), 1 = printf and cpu_assert_debug,
 * which only count here. The FM libraries call our cpu_reset (a counter) either way. */
int config_asser = 1;
const unsigned char log_tag_const_i_FM = 0, log_tag_const_e_FM = 0, log_tag_const_d_FM = 0, log_tag_const_w_FM = 0;
const unsigned char log_tag_const_i_CLOCK = 0, log_tag_const_e_CLOCK = 0, log_tag_const_d_CLOCK = 0, log_tag_const_w_CLOCK = 0;
const unsigned char log_tag_const_i_DAC = 0, log_tag_const_e_DAC = 0, log_tag_const_d_DAC = 0, log_tag_const_w_DAC = 0;
uint32_t gFmAsserts, gFmResets;

int printf(const char *f, ...) { (void)f; return 0; }
int puts(const char *s) { (void)s; return 0; }
int putchar(int c) { return c; }
void log_print(int level, const char *tag, const char *fmt, ...) { (void)level; (void)tag; (void)fmt; }
void cpu_assert_debug(void) { glue_show(7); gFmAsserts++; }
/* JieLi's libraries reset the chip when an allocation fails; we count it and carry on (the FM screen shows it) */
void p33_soft_reset(void) { glue_show(8); gFmResets++; }
void cpu_reset(void) { glue_show(9); gFmResets++; }

/* the FM library asks whether a station scan is running (it then reads cached levels): never, here */
int get_fm_scan_status(void) { return 0; }

/* ------------------------------------------------------------------------------------------- OS (DAC driver) */
/* Our use of the DAC driver is single-threaded (init, trim, rate, start, stop, gains): nothing waits. */
typedef struct { int dummy; } OsObj_t;
int os_sem_create(void *s, int n) { (void)s; (void)n; return 0; }
int os_sem_pend(void *s, int t) { (void)s; (void)t; return 0; }
int os_sem_post(void *s) { (void)s; return 0; }
int os_sem_set(void *s, int n) { (void)s; (void)n; return 0; }
int os_mutex_create(void *m) { (void)m; return 0; }
int os_mutex_pend(void *m, int t) { (void)m; (void)t; return 0; }
int os_mutex_post(void *m) { (void)m; return 0; }
__attribute__((weak)) uint16_t usr_timeout_add(void *p, void (*f)(void *), uint32_t ms, uint8_t pri) { (void)p; (void)f; (void)ms; (void)pri; return 0; }
__attribute__((weak)) void usr_timeout_del(uint16_t id) { (void)id; }
int audio_stream_resume(void *e) { (void)e; return 0; }
int audio_stream_run(void *e, void *d) { (void)e; (void)d; return 0; }
int audio_sample_sync_output_begin(void *s, int n) { (void)s; (void)n; return 0; }
int audio_sample_sync_set_device(void *s, uint8_t d) { (void)s; (void)d; return 0; }
int audio_sample_sync_stop(void *s) { (void)s; return 0; }
int audio_sample_sync_update_count(void *s, int n) { (void)s; (void)n; return 0; }
int audio_sample_sync_us_time_distance(void *s, uint32_t t) { (void)s; (void)t; return 0; }
