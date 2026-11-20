# Open-UV5R-Mini

An open-source firmware effort for the **Baofeng UV-5R Mini**, in the spirit of
[egzumer's UV-K5 firmware](https://github.com/egzumer/uv-k5-firmware-custom), built only with
open tools.

The UV-5R Mini is not an ARM radio like the UV-K5: its MCU is a **JieLi AC695N** (pi32v2 core,
with built-in Bluetooth). The open pi32v2 assembler/LLVM toolchain from
[jvitkauskas/pi32v2-isa](https://github.com/jvitkauskas/pi32v2-isa) makes building for it
possible without JieLi's closed compiler.

## Status

Reverse engineering. RF chip identified as **BK4819** (same as UV-K5). See [docs/FIRMWARE.md](docs/FIRMWARE.md) for the stock firmware format and
[docs/RE-NOTES.md](docs/RE-NOTES.md) for hardware findings.

## Tools

| Tool | What |
| --- | --- |
| `tools/jl_ufw_unpack.py` | Decrypt and unpack a stock `.ufw` update into `flash.bin`, `uboot.boot`, `app.bin` |
| `tools/disasm.py` | Disassemble `app.bin` with the pi32v2-isa table, with string cross-references |

```powershell
python tools/jl_ufw_unpack.py stock-nrf-v0.27.ufw firmware/stock
```

No stock firmware or anything derived from it is committed; bring your own `.ufw`.

## Licence

TBD.
