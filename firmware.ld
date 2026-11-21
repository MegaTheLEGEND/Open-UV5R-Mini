/* Open-UV5R-Mini linker script (JieLi AC695N, UV-5R Mini).
 *
 * Code: the app.bin slot of the stock image, mapped at 0x01E00100 (flash window at 0x01E00000; the stock
 *   app.bin is 0x77AC8 bytes).
 * RAM:  our .data and .bss at 0x14000..0x22000, inside the range the stock app keeps for its own variables, so
 *   the low RAM and whatever the mask-ROM routines use stay as the bootloader left them. The stacks are at
 *   0xD910 (start.S), as stock.
 */
ENTRY(_start)
MEMORY {
  flash (rx) : ORIGIN = 0x01E00100, LENGTH = 0x77B00
  ram (rwx)  : ORIGIN = 0x00014000, LENGTH = 0xE000
}
SECTIONS {
  .entry : { KEEP(*(.entry)) } > flash
  .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > flash
  .data : ALIGN(4) { __data_start = .; *(.data*) . = ALIGN(4); __data_end = .; } > ram AT> flash
  __data_load = LOADADDR(.data);
  .bss (NOLOAD) : ALIGN(4) { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > ram
}
