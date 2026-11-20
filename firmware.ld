/* Open-UV5R-Mini linker script (JieLi AC695N, UV-5R Mini).
 *
 * Code: the app.bin slot of the stock image, mapped at 0x01E00100 (flash window at 0x01E00000; the stock
 *   app.bin is 0x77AC8 bytes, the VM settings area follows at flash 0x97000).
 * RAM:  the stock firmware uses 0x00000..0x228E4 (data and RAM code from 0, stacks at 0xD910, .bss up to
 *   0x228E4). This keeps to 0x00000..0x20000 and puts the stacks at the top.
 */
ENTRY(_start)
MEMORY {
  flash (rx) : ORIGIN = 0x01E00100, LENGTH = 0x77B00
  ram (rwx)  : ORIGIN = 0x00000000, LENGTH = 0x20000
}
SECTIONS {
  .entry : { KEEP(*(.entry)) } > flash
  .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > flash
  .data : ALIGN(4) { __data_start = .; *(.data*) . = ALIGN(4); __data_end = .; } > ram AT> flash
  __data_load = LOADADDR(.data);
  .bss (NOLOAD) : ALIGN(4) { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > ram
  __sstack_top = ORIGIN(ram) + LENGTH(ram) - 0x1000;
  __stack_top = ORIGIN(ram) + LENGTH(ram);
}
