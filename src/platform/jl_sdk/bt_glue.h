/* Open-UV5R-Mini: the stand-ins JieLi's Bluetooth controller needs (bt_glue.c), as used by the BLE backend (bt_le.c). */
#ifndef PLATFORM_JL_SDK_BT_GLUE_H
#define PLATFORM_JL_SDK_BT_GLUE_H

#include <stdbool.h>
#include <stdint.h>
#include "driver/irq.h"

/* Bluetooth's RAM: low RAM 0x3000..0xC000 (firmware.ld region "btram"), free under our firmware (stock's RAM code
 * ran there). JieLi's controller keeps its variables there too; bt_le.c clears it and copies the initial values in
 * at every Bluetooth start, so it is set up fresh each time. */
#define BTRAM_BSS __attribute__((section(".btram.bss")))

static inline uint32_t BTGLUE_IrqSave(void) { return IRQ_Save(); }
static inline void BTGLUE_IrqRestore(uint32_t w) { IRQ_Restore(w); }

/* the controller task's message queue (os_taskq_post_type / os_taskq_pend) */
void BTGLUE_QueueReset(void);
int  os_taskq_pend(const char *fmt, int *argv, int argc);

/* the software timers the controller asks for (sys_timer_add and its kin), run from the main loop */
void BTGLUE_TimersReset(uint32_t now_ms);
void BTGLUE_TimersRun(uint32_t now_ms);

/* the heap (fm_glue.c): while on, malloc takes from the Bluetooth heap in btram (base, size), free goes back to
 * whichever heap a block came from, so the key beeps' DAC driver keeps its blocks in the FM heap */
void FMGLUE_BtHeap(bool on, void *base, uint32_t size);

extern uint32_t gBtQFull, gBtQPosts, gBtTimerFull;

#endif
