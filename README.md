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

**C port:** early. What still has to be written before it is a working firmware, and in what order, is in the
**[road map](docs/ROADMAP.md)**.

### What you can use it for today

The C firmware isn't ready, but the rest of the repo is useful now:

- **Build stock v0.27 from source with open tools.** `tools/build_matching.py` rebuilds it byte for byte, with no
  JieLi compiler or SDK. That gives an editable source for the stock firmware: small same-size changes (text,
  values, single instructions) build and pass every check. Try the `5RMINI` → `OPENUV` demo in
  [docs/MATCHING.md](docs/MATCHING.md).
- **Flash a radio with your own build, or put it back to stock.** The build writes a patch that
  [Codeplug Studio](https://codeplug.mm9pdy.net)'s flasher accepts ([docs/TESTING.md](docs/TESTING.md)).
- **Practise flashing without a radio.** `tools/uv5r_flasher.py` has a built-in emulated radio: a full update,
  a power cut part way, resuming, and a boot check, all on the PC. Its serial path has not been tried on a real
  radio yet.
- **Inspect and check `.ufw` files.** Unpack them (`tools/jl_ufw_unpack.py`), verify every CRC
  (`tools/ufwcheck.py`), and run the flasher's safety checks before anything goes near a radio
  (`tools/flashcheck.py`). `tools/mkufw.py` refuses images that would leave a radio impossible to flash again.
- **Reference for anyone working on this radio.** Covers the `.ufw` format and its cipher, the flash layout, the
  pin map (LCD, keypad, PTT, BK4819, power), and the stock update protocol and hand-over. Also covers the ROM
  routines identified so far and the second flash chip.
  See [docs/FIRMWARE.md](docs/FIRMWARE.md), [docs/RE-NOTES.md](docs/RE-NOTES.md),
  [docs/UPDATE-MODE.md](docs/UPDATE-MODE.md) and [docs/EMULATOR.md](docs/EMULATOR.md).
- **Reverse-engineering tools.** A disassembly with named functions (`tools/disasm.py`, `tools/symbols.txt`), and
  a CPU emulator that runs the real firmware part way, for tracing what the stock code does (`emu/`).
- **See any stock function as C.** The lifter (`tools/lift/`) turns the whole stock app into compilable C in
  seconds and checks it against the stock code instruction by instruction. Use it to understand a function before
  modding it, or as the starting point for rewriting it. See [docs/LIFTER.md](docs/LIFTER.md).

There are no new radio features yet, and the C build must not be flashed (it can't yet be flashed again
afterwards).

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
| `tools/lift/lift.py` | Lift the stock app to C (`build/lift/`); `insn_test.py` and `func_test.py` check it against the ISA models and the stock code ([docs/LIFTER.md](docs/LIFTER.md)) |
| `emu/loader_store.py`, `emu/uart_pins.py` | Run stock routines in the emulator to learn what they do (loader store address, cable pin setup) |
| `tests/update_host.py` | Runs the C update mode on the PC against the real flasher host ([docs/UPDATE-MODE.md](docs/UPDATE-MODE.md)) |
| `tools/ufwpatch.py` | Apply or make patches in the format Codeplug Studio's flasher reads |

```powershell
python tools/jl_ufw_unpack.py stock-nrf-v0.27.ufw firmware/stock
```

No stock firmware or anything derived from it is committed; bring your own `.ufw`.

## Licence

TBD.
