# Open-UV5R-Mini

Open-source firmware for the **Baofeng UV-5R Mini**, built only with open tools. The idea is the same as the UV-K5's
open firmware: a radio you can change in plain C.

**Not ready to use on a radio yet.** The firmware is being written from scratch. The stock firmware is used only
as a reference for how the hardware works.

## Which radio

There are two kinds of UV-5R Mini with `Hardware:V1.7`. They are the same radio, with two builds of the same stock
firmware. To see which one you have:
- **MENU › Radio Info** shows the Firmware line.
- Switch the radio off, then hold **8** while you switch it on. For about a second the screen shows the suffix.

| Suffix | Firmware | Stock file | Status here |
| --- | --- | --- | --- |
| `5RMINI NRF` | `V0.27` | [stock-nrf-v0.27.ufw](https://mega.nz/file/U5NSDIhZ#DqRVur6RPVay1ylKZFQsz2zrXnR3ZrAMBToPHgPnPvQ) | main target |
| `5RMINI NRFB` | `V0.04` | [stock-nrfb-v0.04.ufw](https://mega.nz/file/NgFkWZTS#l4b9-Z6dSISLWBNc8OWMHYUfmzyFV_WT9f268APxnt0) | unpacked, disassembled and lifted to C; builds still NRF-only |

The two builds differ only slightly. NRFB's app is 596 bytes bigger, and that moves the settings area along by
4 KB. Never flash a file made for one kind onto the other.

The stock files are unofficial copies shared by a third party, the same ones
[Codeplug Studio](https://codeplug.mm9pdy.net) links to. This repo only links to them. `tools/setup.py` checks
the file against its pinned SHA-256 and refuses any other file.

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

Download the stock file for your radio (links above) into `firmware/`. It isn't in this repo, because it's
Baofeng's code.

```powershell
python tools/setup.py                                                      # check and unpack it into build/
python tools/build_matching.py --toolchain <pi32v2-llvm>/bin --isa <pi32v2-isa>  # rebuild stock from source
python tools/uv5r_flasher.py                                               # flasher, with an emulated radio to practise on
```

Building the C firmware needs the open [pi32v2 toolchain](https://github.com/jvitkauskas/pi32v2-isa).
See [docs/PORTING.md](docs/PORTING.md).

Using Claude Code or another AI agent? Point it at [AGENTS.md](AGENTS.md): the rules, the context and how to
contribute.

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
