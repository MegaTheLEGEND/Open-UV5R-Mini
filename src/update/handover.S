/* Open-UV5R-Mini: the last step of the cable update: reset into the boot ROM, which then runs the loader that the
 * update mode stored (update/update_radio.c puts the hand-over record at 0x2FF88 first).
 *
 * Runs from RAM (.ramfunc): it switches the flash controller off, after which nothing can be fetched from the flash.
 * What it does, step for step, is what the stock app's hand-over does (stock RAM code 0x020011E6 with 0x02000F18,
 * 0x02000F00 and 0x02000AF0, and the boot ROM's flash lock at 0x112250 called with 0); written fresh here:
 *   stacks to 0x10C20 (ssp, sp) and 0x11020 (usp);
 *   0x100300 bit 0 off;
 *   0x1E0008 bits 8-10 = 3;
 *   flash controller off: wait 0x101000 bit 5, wait SFC CON (0x1F0200) bit 31 clear, CON bit 0 off;
 *   0x1E000C |= 1, |= 2, a short wait, &= ~0x1C0; 0x1E0040 bit 0 off; 0x1E0008 = 0;
 *   every register 0, then rti to 0x110000 (the boot ROM).
 * Not yet run on a radio.
 */
	.section .ramfunc,"ax"
	.globl UPDATE_JumpToBoot
UPDATE_JumpToBoot:
	ssp = 0x10C20
	sp = ssp
	usp = 0x11020
	r0 = 0x100300
	r1 = [r0+0]
	r1 = r1 & 0xFFFFFFFE
	[r0+0] = r1
	r0 = 0x1E0008
	r1 = [r0+0]
	r1 = r1 & 0xFFFFF8FF
	r1 = r1 | 0x300
	[r0+0] = r1
	r0 = 0x101000
1:	r1 = [r0+0]
	r1 = r1 & 0x20
	if (r1 == 0) goto 1b
	r0 = 0x1F0200
2:	r1 = [r0+0]
	ifs (r1 < 0) goto 2b
	r1 = r1 & 0xFFFFFFFE
	[r0+0] = r1
	r0 = 0x1E000C
	r1 = [r0+0]
	r1 = r1 | 0x1
	[r0+0] = r1
	r1 = [r0+0]
	r1 = r1 | 0x2
	[r0+0] = r1
	r2 = 100
3:	r2 = r2 + -1
	if (r2 != 0) goto 3b
	r1 = [r0+0]
	r1 = r1 & 0xFFFFFE3F
	[r0+0] = r1
	r0 = 0x1E0040
	r1 = [r0+0]
	r1 = r1 & 0xFFFFFFFE
	[r0+0] = r1
	r0 = 0x1E0008
	r1 = 0
	[r0+0] = r1
	r1_r0 = 0
	r3_r2 = 0
	r5_r4 = 0
	r7_r6 = 0
	r9_r8 = 0
	r11_r10 = 0
	r13_r12 = 0
	r15_r14 = 0
	psr = r0
	rete = r0
	retx = r0
	rets = r0
	reti = r0
	reti = 0x110000
	rti
