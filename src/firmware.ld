/* Open-UV5R-Mini linker script (JieLi AC695N, UV-5R Mini).
 *
 * Code: the app.bin slot of the stock image, mapped at 0x01E00100 (flash window at 0x01E00000; the stock
 *   app.bin is 0x77AC8 bytes).
 * RAM code (.ramfunc): code that must not run from the flash (the flash driver). It is copied by start.S to RAM
 *   0x1000 and runs at 0x02001000, the instruction alias of RAM, as the stock app runs its own RAM code
 *   (docs/RE-NOTES.md, "First boot").
 * RAM:  our .data and .bss at 0x14000..0x22000, inside the range the stock app keeps for its own variables, so
 *   the low RAM stays as the bootloader left it. The stacks are at 0xD910 (start.S), as stock.
 */
ENTRY(_start)
MEMORY {
  flash (rx) : ORIGIN = 0x01E00100, LENGTH = 0x77B00
  ramx (rx)  : ORIGIN = 0x02001000, LENGTH = 0x2000
  ram (rwx)  : ORIGIN = 0x00014000, LENGTH = 0xE000
}
SECTIONS {
  .entry : { KEEP(*(.entry)) } > flash
  .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > flash
  .ramfunc : ALIGN(4) { __ramfunc_start = .; *(.ramfunc*) . = ALIGN(4); __ramfunc_end = .; } > ramx AT> flash
  __ramfunc_load = LOADADDR(.ramfunc);
  .data : ALIGN(4) { __data_start = .; *(.data*) . = ALIGN(4); __data_end = .; } > ram AT> flash
  __data_load = LOADADDR(.data);
  .bss (NOLOAD) : ALIGN(4) { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > ram
}
