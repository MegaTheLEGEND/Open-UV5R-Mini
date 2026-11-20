/* Open-UV5R-Mini: start-up for the AC695N application slot.
 *
 * The stock app.bin begins at 0x01E00100 (flash window) and its first job is the same as this one: set the
 * stacks, copy .data to RAM, clear .bss, call main. Based on the MIT-0 crt0.S of jvitkauskas/pi32v2-isa
 * (examples/fm1-blink).
 */
	.section .entry,"ax"
	.globl _start
_start:
	cli
	ssp = __sstack_top
	sp = __stack_top
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
