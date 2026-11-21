/* Like firmware.ld, but our RAM starts at 0x14000 so the low RAM the stock app uses stays as the bootloader left
   it; the stacks are set in start_like_stock.S. */
ENTRY(_start)
MEMORY {
  flash (rx) : ORIGIN = 0x01E00100, LENGTH = 0x77B00
  ram (rwx)  : ORIGIN = 0x00014000, LENGTH = 0x4000
}
SECTIONS {
  .entry : { KEEP(*(.entry)) } > flash
  .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > flash
  .data : ALIGN(4) { __data_start = .; *(.data*) . = ALIGN(4); __data_end = .; } > ram AT> flash
  __data_load = LOADADDR(.data);
  .bss (NOLOAD) : ALIGN(4) { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > ram
}
