/* Open-UV5R-Mini: interrupts (AC695N, pi32v2 core).
 *
 * Until the FM radio (2026-11-24) the firmware ran with every interrupt off and polled everything. JieLi's FM
 * receiver needs its interrupt (43, a buffer of samples every 0.85 ms), so this is the smallest set to give it one.
 *
 * Facts (JieLi's public BR23 SDK headers, Apache-2.0, and the stock request_irq at stock `sub_01E1EAA0`):
 *   - the vector table is fixed in hardware at RAM 0x2BF00: 64 handler addresses, one per interrupt number;
 *   - IPCON at 0x10F100 + 4 * (n / 8): a nibble per interrupt, bit 0 on, bits 1-3 the priority;
 *   - IPMASK at 0x10F1A8: interrupts at or below this priority are held (JieLi's local_irq_disable writes 6);
 *   - the core's global enable is ICFG bits 8 and 9 (JieLi's start-up clears them; we never set them before).
 * A handler is a C function with IRQ_HANDLER (it saves what it uses and returns with rti).
 *
 * Code running from the internal flash must not be interrupted while the flash is being written (the handler may
 * sit in the flash): IRQ_Save / IRQ_Restore are inline, so the RAM-code flash driver can use them. */
#ifndef DRIVER_IRQ_H
#define DRIVER_IRQ_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __PI32V2__
#define IRQ_HANDLER __attribute__((interrupt("")))
#else
#define IRQ_HANDLER   /* the host syntax check (make check) */
#endif

#define IRQ_AUDIO  12
#define IRQ_SARADC 23
#define IRQ_FMRX   43
#define IRQ_COUNT  64

/* Every handler set to a do-nothing one, every interrupt off, IPMASK 0; the global enable stays as it is. */
void IRQ_Init(void);
/* Install handler h for interrupt n with priority 0-7 and turn it on. */
void IRQ_Request(uint8_t n, uint8_t priority, void (*h)(void));
void IRQ_Free(uint8_t n);
/* The core's global enable (ICFG bits 8-9). */
void IRQ_GlobalOn(void);
void IRQ_GlobalOff(void);
bool IRQ_GlobalIsOn(void);
/* Interrupts serviced since IRQ_Init (any number), for the FM screen's diagnostics. */
extern volatile uint32_t gIrqCount[IRQ_COUNT];

#ifdef __PI32V2__
static inline __attribute__((always_inline)) uint32_t IRQ_Save(void)
{
	uint32_t c, n;
	__asm__ volatile("%0 = icfg" : "=r"(c));
	n = c & ~0x300u;
	__asm__ volatile("icfg = %0" ::"r"(n));
	__asm__ volatile("csync");
	return c & 0x300u;
}

static inline __attribute__((always_inline)) void IRQ_Restore(uint32_t was)
{
	uint32_t c;
	if (!was)
		return;
	__asm__ volatile("%0 = icfg" : "=r"(c));
	c |= was;
	__asm__ volatile("icfg = %0" ::"r"(c));
}
#else
static inline uint32_t IRQ_Save(void) { return 0; }
static inline void IRQ_Restore(uint32_t was) { (void)was; }
#endif

#endif
