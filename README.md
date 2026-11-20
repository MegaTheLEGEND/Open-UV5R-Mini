# Open-UV5R-Mini

Open-source firmware for the **Baofeng UV-5R Mini**, built only with open tools. The idea is the same as the UV-K5's
open firmware: a radio you can change in plain C.

**Not ready to use on a radio yet.** The firmware is being written from scratch. The stock firmware is used only
as a reference for how the hardware works.

## Which radio

Only the **NRF** model: Radio Info shows `Firmware:V0.27` and `Hardware:V1.7`, and holding **8** while switching
on shows `5RMINI NRF`. Radios on `NRFB V0.04` have a different flash layout: don't flash anything from here on them.

## How far it is

| Part | State |
| --- | --- |
| Stock firmware rebuilt from source with open tools, byte for byte | done |
| Small mods to the stock firmware, as a flashable patch (demo: boot text `OPENUV NRF`) | done, not yet tried on a radio |
| Flasher with a built-in emulated radio, for practice | done |
| Stock code turned into C and checked against the original (reference for porting) | done |
| Own firmware: update mode (so a radio can always be flashed again) | written; tested on the PC; next: emulator, then radio |
| Own firmware: drivers (radio chip, display, keypad, UART) | basic versions |
| Own firmware: clocks, timers, power, battery, audio, LEDs | mostly to do |
| Own firmware: radio features (channels, menus, scanning, …) | to do |
| Bluetooth | to do; optional, through JieLi's own libraries |

Details and the order of work: **[docs/ROADMAP.md](docs/ROADMAP.md)**.

## Find your way

| Folder | What's in it |
| --- | --- |
| `driver/` | Hardware drivers: `bk4819` (radio chip), `st7735s` (display), `keyboard`, `uart`, `gpio`, `system` |
| `app/` | Application: main logic and update mode |
| `ui/` | Screen drawing and font |
| `bt/` | Bluetooth (empty stub in the default build) |
| `main.c`, `board.c`, `start.S`, `firmware.ld` | Start-up, board setup, memory layout |
| `bsp/`, `platform/` | Chip registers (AC695N) and SDK glue |
| `tools/` | Unpack, build, check and flash `.ufw` files; disassembler; `tools/lift/` turns stock code into C |
| `emu/` | Emulator that runs the real firmware on the PC, for tracing what the stock code does |
| `tests/` | Tests run on the PC |
| `docs/` | Everything else, listed below |
| `build/` | Generated locally; never committed (it holds stock-derived files) |

## Getting started

You need your own copy of the stock firmware, `stock-nrf-v0.27.ufw`. It isn't in this repo, because it's Baofeng's
code. Put it in `firmware/`.

```powershell
python tools/jl_ufw_unpack.py firmware/stock-nrf-v0.27.ufw firmware/stock   # look inside the stock firmware
python tools/build_matching.py --toolchain <pi32v2-llvm>/bin --isa <pi32v2-isa>  # rebuild stock from source
python tools/uv5r_flasher.py                                               # flasher, with an emulated radio to practise on
```

Building the C firmware needs the open [pi32v2 toolchain](https://github.com/jvitkauskas/pi32v2-isa).
See [docs/PORTING.md](docs/PORTING.md).

## Docs

| Doc | Read it for |
| --- | --- |
| [ROADMAP](docs/ROADMAP.md) | What's done and what's next |
| [TESTING](docs/TESTING.md) | Flashing a radio safely, and getting back to stock |
| [PORTING](docs/PORTING.md) | Building the C firmware |
| [MATCHING](docs/MATCHING.md) | The byte-for-byte stock rebuild, and making small mods |
| [RE-NOTES](docs/RE-NOTES.md) | Hardware findings: pins, chips, memory |
| [FIRMWARE](docs/FIRMWARE.md) | The `.ufw` file format |
| [UPDATE-MODE](docs/UPDATE-MODE.md) | How the radio receives new firmware |
| [EMULATOR](docs/EMULATOR.md) | The emulator |
| [LIFTER](docs/LIFTER.md) | Stock code to C, and the AI rewrite tool |
| [BLUETOOTH](docs/BLUETOOTH.md) | Plans for Bluetooth |

## Licence

TBD.
