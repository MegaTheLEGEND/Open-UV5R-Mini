/* RAM program for the ROM download mode: code at 0x12000 (where JieLi's own loaders go), request at 0x12F00,
   buffer at 0x13000-0x14000. */
ENTRY(extflash_entry)
SECTIONS
{
	. = 0x12000;
	.text : { KEEP(*(.text.entry)) *(.text*) *(.rodata*) }
	.data : { *(.data*) *(.bss*) *(COMMON) }
	ASSERT(. <= 0x12F00, "program overlaps the request block")
	/DISCARD/ : { *(.comment) *(.note*) }
}
