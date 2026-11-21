# jl_extflash: read the external SPI flash over the ROM USB download mode

The radio's 8-pin flash (GT25Q16A, 2 MB, channels and probably fonts and pictures) sits on SPI1, which
jl-uboot-tool's loader doesn't reach. This puts a small program of our own in the chip's RAM (nothing goes in any
flash), which bit-bangs the flash's READ command and hands the data to the PC through the ROM's memory read.

It only reads. **Worked on one NRF V0.27 radio (2026-11-21):** ID `C46015`, the whole 2 MB in 22 s, two reads
identical. What the chip holds is in [RE-NOTES](../../docs/RE-NOTES.md#external-spi-flash-read-2026-11-21).

1. Enter the ROM download mode with [jl_usbkey](../jl_usbkey/README.md). The PC shows `BR23 UBOOT1.00`. Don't run
   jl-uboot-tool first: this needs the ROM itself, not its loader.
2. Build the RAM program:

   ```powershell
   E:\toolchains\pi32v2-llvm\bin\clang --target=pi32v2 -Os -ffreestanding -fno-builtin -c tools/jl_extflash/extflash.c -o build/extflash/extflash.o
   E:\toolchains\pi32v2-llvm\bin\ld.lld -T tools/jl_extflash/extflash.ld build/extflash/extflash.o -o build/extflash/extflash.elf
   E:\toolchains\pi32v2-llvm\bin\llvm-objcopy -O binary build/extflash/extflash.elf build/extflash/extflash.bin
   ```

3. In an administrator PowerShell, with jl-uboot-tool's packages installed:

   ```powershell
   python tools/jl_extflash/extflash.py --device \\.\PHYSICALDRIVE3 -o firmware/backups/external.bin
   ```

It reads the JEDEC ID first (`C46015` on the radio tried) and stops if there's no answer, then reads the chip twice and
compares.

| RAM | Use |
| --- | --- |
| `0x12000` | the program (where JieLi's loaders go) |
| `0x12F00` | request and result |
| `0x13000`–`0x14000` | 4 KB data buffer |
