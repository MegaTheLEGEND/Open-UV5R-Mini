/* Open-UV5R-Mini: crash record, exception handlers and the safe start (driver/crash.h). */
#include "driver/crash.h"
#include "driver/irq.h"

#define CRASH_MAGIC 0x43524153u      /* "CRAS" */

typedef struct {
	uint32_t magic, check;           /* check = ~magic: anything else is RAM after a power cut */
	uint32_t running;                /* 1 from start-up until a deliberate power-off */
	uint32_t early;                  /* restarts in a row that came before CRASH_STABLE_MS */
	uint32_t vec;                    /* the exception: vector + 1, 0 = none (a watchdog restart) */
	uint32_t pc, rets;
	uint32_t up_ms;                  /* how long the run went, roughly (to CRASH_STABLE_MS) */
} CrashRec_t;

static volatile CrashRec_t R __attribute__((section(".ram2.crash")));
static bool s_restarted, s_safe, s_stable;
static uint32_t s_vec, s_pc;

#ifdef __PI32V2__
/* An exception: note it, then wait without clearing the watchdog, which restarts the radio. */
#define EXC_HANDLER(n)                                                                  \
	static IRQ_HANDLER void exc##n(void)                                                \
	{                                                                                   \
		uint32_t pc, rs;                                                                \
		__asm__ volatile("%0 = reti" : "=r"(pc));                                       \
		__asm__ volatile("%0 = rets" : "=r"(rs));                                       \
		R.vec = (n) + 1u;                                                               \
		R.pc = pc;                                                                      \
		R.rets = rs;                                                                    \
		for (;;)                                                                        \
			;                                                                           \
	}
EXC_HANDLER(0)
EXC_HANDLER(1)
EXC_HANDLER(2)
#endif

void CRASH_Init(void)
{
	bool ok = R.magic == CRASH_MAGIC && R.check == ~CRASH_MAGIC;

	if (ok && R.running) {           /* the last run did not end at the power switch */
		s_restarted = true;
		s_vec = R.vec;
		s_pc = R.pc;
		R.early = R.up_ms < CRASH_STABLE_MS ? R.early + 1u : 0u;
	} else
		R.early = 0;
	s_safe = R.early >= CRASH_SAFE_AFTER;
	R.magic = CRASH_MAGIC;
	R.check = ~CRASH_MAGIC;
	R.vec = R.pc = R.rets = 0;
	R.up_ms = 0;
	R.running = 1;
#ifdef __PI32V2__
	IRQ_SetVector(0, exc0);
	IRQ_SetVector(1, exc1);
	IRQ_SetVector(2, exc2);
#endif
}

void CRASH_Tick(uint32_t uptime_ms)
{
	if (s_stable)
		return;
	R.up_ms = uptime_ms;
	if (uptime_ms >= CRASH_STABLE_MS) {
		s_stable = true;
		R.early = 0;                     /* a good start: the count begins again */
	}
}

void CRASH_CleanOff(void)
{
	R.running = 0;
	R.early = 0;
}

bool CRASH_Restarted(void) { return s_restarted; }
bool CRASH_Safe(void) { return s_safe; }

const char *CRASH_Text(void)
{
	static char t[22];
	char *p = t;
	const char *q;

	if (!s_restarted)
		return 0;
	q = s_vec ? "CRASH E" : "RESTARTED";   /* no exception noted: a hang (watchdog), or one in the FM receiver */
	while (*q)
		*p++ = *q++;
	if (s_vec) {
		*p++ = (char)('0' + (s_vec - 1u) % 10u);
		*p++ = ' ';
		for (int i = 7; i >= 0; i--)
			*p++ = "0123456789ABCDEF"[s_pc >> (4 * i) & 0xFu];
	}
	*p = 0;
	return t;
}
