/* Open-UV5R-Mini: start-up for the AC695N application slot (runs at 0x01E00100).
 *
 * Sets the CPU up the way the stock app does before its first mask-ROM call. Tested on a radio on 2026-11-21
 * (tests/radio/blink2.c): with this start-up the ROM routines at 0x0200xxxx (P33 access, watchdog) work; with
 * our first one (stacks at the top of RAM, our variables at 0, the boot record dropped) the first ROM call
 * restarted the chip. Which of the three matters is not narrowed down yet.
 *   - The bootloader passes a boot record in r0; store the same fields at the same RAM addresses as stock
 *     (sub_01E206F4: 0xD920..0xD947).
 *   - The main stack at 0xD910, as stock. That leaves about 2 KB below it (stock's RAM code ends at 0xD0F4).
 *   - Interrupts get a stack of their own (ssp, irq_stack below). Stock sets both sp and ssp to 0xD910, but its
 *     main code runs as RTOS tasks on stacks of their own, so 0xD910 holds only interrupts. Ours runs on sp, and
 *     with ssp at 0xD910 too every interrupt started at 0xD910 and wrote over the main code's outer stack frames
 *     (seen with the cable monitor, 2026-11-26: the FM interrupt left fm_dem's values where main's return
 *     addresses were, and the radio crashed when the FM start returned).
 *   - Our .data and .bss live at 0x14000 (firmware.ld), away from the low RAM stock uses.
 * Copy loop and .bss clear based on the MIT-0 crt0.S of jvitkauskas/pi32v2-isa (examples/fm1-blink).
 */
	.section .entry,"ax"
	.globl _start
_start:
	r1 = 0xD920
	r3_r2 = d[r0+4]
	d[r1+16] = r3_r2
	r2 = [r0+0]
	r3 = [r2+8]
	[r1+24] = r3
	r2 = b[r2+13] (u)
	b[r1+0] = r2
	r2 = h[r0+12] (u)
	h[r1+28] = r2
	r2 = h[r0+14] (u)
	h[r1+30] = r2
	r3_r2 = d[r0+16]
	d[r1+32] = r3_r2
	sp = 0xD910
	ssp = irq_stack_top
	r1 = __ramfunc_load            /* RAM code: copy to RAM (run address - 0x02000000) */
	r2 = __ramfunc_start
	r3 = __ramfunc_end
	r0 = 0x02000000
	r2 = r2 - r0
	r3 = r3 - r0
6:	if (r2 >= r3) goto 7f
	r0 = [r1+0]
	[r2+0] = r0
	r1 = r1 + 4
	r2 = r2 + 4
	goto 6b
7:	r1 = __data_load
	r2 = __data_start
	r3 = __data_end
1:	if (r2 >= r3) goto 2f
	r0 = [r1+0]
	[r2+0] = r0
	r1 = r1 + 4
	r2 = r2 + 4
	goto 1b
2:	r2 = __bss_start
	r3 = __bss_end
	r0 = 0
3:	if (r2 >= r3) goto 4f
	[r2+0] = r0
	r2 = r2 + 4
	goto 3b
4:	call main
5:	goto 5b

/* the interrupt stack (ssp): 2 KB, cleared with the rest of .bss before interrupts are turned on */
	.section .bss.irq_stack,"aw",@nobits
	.balign 8
irq_stack:
	.space 2048
	.globl irq_stack_top
irq_stack_top:
