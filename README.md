# Open-UV5R-Mini

An open-source firmware effort for the **Baofeng UV-5R Mini**, in the spirit of
[egzumer's UV-K5 firmware](https://github.com/egzumer/uv-k5-firmware-custom), built only with
open tools.

The UV-5R Mini is not an ARM radio like the UV-K5: its MCU is a **JieLi AC695N** (pi32v2 core,
with built-in Bluetooth). The open pi32v2 assembler/LLVM toolchain from
[jvitkauskas/pi32v2-isa](https://github.com/jvitkauskas/pi32v2-isa) makes building for it
possible without JieLi's closed compiler.

## Status

**Matching build done:** the stock v0.27 firmware is rebuilt from source with only open tools, byte for byte
(`app.bin`, the `.ufw`, and the flashable image with loader all identical). It is the safe base for testing on a
radio and for replacing functions with C. See [docs/MATCHING.md](docs/MATCHING.md) and
[docs/TESTING.md](docs/TESTING.md).


Reverse engineering. RF chip identified as **BK4819** (same as UV-K5). See [docs/FIRMWARE.md](docs/FIRMWARE.md) for the stock firmware format and
[docs/RE-NOTES.md](docs/RE-NOTES.md) for hardware findings.

## Firmware (C port)

The first image is a key-test screen built on egzumer-style drivers for the BK4819, ST7735S LCD and keypad.
See [docs/PORTING.md](docs/PORTING.md) for status, build steps and what is still unknown.

The default build is fully open and has no Bluetooth. An optional Bluetooth edition links JieLi's closed
Bluetooth libraries from your own SDK copy; this repo never contains them. See [docs/BLUETOOTH.md](docs/BLUETOOTH.md).

## Tools

| Tool | What |
| --- | --- |
| `tools/jl_ufw_unpack.py` | Decrypt and unpack a stock `.ufw` update into `flash.bin`, `uboot.boot`, `app.bin` |
| `tools/disasm.py` | Disassemble `app.bin` with the pi32v2-isa table, with string cross-references |
| `tools/mkufw.py` | Put a built app image into a stock `.ufw`, re-encrypting and fixing every CRC |
| `tools/ufwcheck.py` | Verify every CRC in a `.ufw` |
| `tools/mkasm.py` | Turn the stock `app.bin` into assembly that rebuilds byte for byte |
| `tools/build_matching.py` | Matching build end to end: source, rebuild, `.ufw`, flashable image, checks, flasher patch |
| `tools/flashcheck.py` | The flasher's image checks, run before anything goes near a radio |
| `tools/uv5r_flasher.py` | GUI flasher with a built-in emulated radio (protocol + flash) for practice flashes |
| `emu/uv5r_emu.py` | Emulator running the real firmware (work in progress, see [docs/EMULATOR.md](docs/EMULATOR.md)) |
| `tools/ufwpatch.py` | Apply or make patches in the format Codeplug Studio's flasher reads |

```powershell
python tools/jl_ufw_unpack.py stock-nrf-v0.27.ufw firmware/stock
```

No stock firmware or anything derived from it is committed; bring your own `.ufw`.

## Licence

TBD.
