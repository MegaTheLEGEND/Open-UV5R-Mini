/* Open-UV5R-Mini: link script for the matching rebuild of the stock v0.27 app.bin.
 * The whole stock image is one section placed where the bootloader runs it. */
ENTRY(stock_app_start)
MEMORY {
  flash (rx) : ORIGIN = 0x01E00100, LENGTH = 0x77B00
}
SECTIONS {
  .stock_app : { KEEP(*(.stock_app)) } > flash
  /DISCARD/ : { *(.text) *(.data) *(.bss) }
}
