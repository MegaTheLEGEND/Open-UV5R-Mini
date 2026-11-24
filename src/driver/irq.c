/* Open-UV5R-Mini: interrupts. See driver/irq.h. */
#include "driver/irq.h"
#include "bsp/ac695n.h"

#define IRQ_TABLE   ((volatile uint32_t *)0x0002BF00u)
#define IRQ_IPCON(n) REG32((uintptr_t)0x0010F100u + 4u * (uintptr_t)(n))
#define IRQ_IPMASK  REG32(0x0010F1A8u)

volatile uint32_t gIrqCount[IRQ_COUNT];

/* An interrupt nobody asked for: counted (number unknown here) and ignored. Its source stays on, so it would come
 * back at once; IRQ_Init turns every source off first, so this only catches mistakes. */
static IRQ_HANDLER void irq_spurious(void)
{
	gIrqCount[0]++;
}

void IRQ_Init(void)
{
	uint32_t was = IRQ_Save();

	for (unsigned i = 0; i < IRQ_COUNT; i++) {
		IRQ_TABLE[i] = (uint32_t)(uintptr_t)irq_spurious;
		gIrqCount[i] = 0;
	}
	for (unsigned i = 0; i < 32u; i++)   /* as JieLi's interrupt_init: all 32 IPCON words off */
		IRQ_IPCON(i) = 0;
	IRQ_IPMASK = 0;
	IRQ_Restore(was);
}

void IRQ_Request(uint8_t n, uint8_t priority, void (*h)(void))
{
	uint32_t was, sh = (uint32_t)(n & 7u) * 4u, v;

	if (n >= IRQ_COUNT || !h)
		return;
	was = IRQ_Save();
	IRQ_TABLE[n] = (uint32_t)(uintptr_t)h;
	v = IRQ_IPCON(n / 8u) & ~(0xFu << sh);
	IRQ_IPCON(n / 8u) = v | ((1u | (uint32_t)(priority & 7u) << 1) << sh);
	IRQ_Restore(was);
}

void IRQ_SetVector(uint8_t n, void (*h)(void))
{
	if (n < IRQ_COUNT && h)
		IRQ_TABLE[n] = (uint32_t)(uintptr_t)h;
}

void IRQ_Free(uint8_t n)
{
	uint32_t was;

	if (n >= IRQ_COUNT)
		return;
	was = IRQ_Save();
	IRQ_IPCON(n / 8u) &= ~(0xFu << ((uint32_t)(n & 7u) * 4u));
	IRQ_TABLE[n] = (uint32_t)(uintptr_t)irq_spurious;
	IRQ_Restore(was);
}

void IRQ_GlobalOn(void)
{
	IRQ_Restore(0x300u);
}

void IRQ_GlobalOff(void)
{
	(void)IRQ_Save();
}

bool IRQ_GlobalIsOn(void)
{
	uint32_t c = 0;
#ifdef __PI32V2__
	__asm__ volatile("%0 = icfg" : "=r"(c));
#endif
	return (c & 0x300u) == 0x300u;
}
