/* Open-UV5R-Mini: test start-up that sets up the CPU the way the stock app does before its first mask-ROM call
 * (stock 0x01E00100 and sub_01E206F4), to find what the ROM routines at 0x0200xxxx need:
 *   - keep the boot-info record the bootloader passes in r0: the same fields stored at the same RAM addresses
 *     (0xD920..0xD947) as the stock app;
 *   - stacks at 0xD910, as stock;
 *   - leave RAM below 0xD960 alone otherwise (our variables live at 0x14000, see blink2.ld).
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
	ssp = 0xD910
	r1 = __data_load
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
