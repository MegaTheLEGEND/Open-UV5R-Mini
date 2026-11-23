# Guide for AI agents (Claude Code and others)

Read this before you change anything. It's the context a human contributor builds up over weeks.

## The project in five lines

- Open firmware for the **Baofeng UV-5R Mini** in C, built only with open tools: our own code, plus third-party
  code (open or proprietary) whose licence lets us use and redistribute it ([docs/THIRD-PARTY.md](docs/THIRD-PARTY.md)).
- MCU: **JieLi AC695N**, a **pi32v2** core (not ARM), with built-in Bluetooth. RF chip: **Beken BK4819**, the same
  chip as the Quansheng UV-K5. LCD: ST7735S, 128×128.
- The stock firmware is used only as a **reference** for how the hardware works. The repo has tools to read it: an
  unpacker, a disassembler, a lifter to C, and an emulator.
- Two kinds of radio, both `Hardware:V1.7`: **NRF** (stock V0.27, the main target) and **NRFB** (stock V0.04). They
  have the same board but a different flash layout.
- Status: the update mode is written and tested on the PC; drivers are basic; most radio features are still to do.
  What's left is in [docs/ROADMAP.md](docs/ROADMAP.md).

## Rules (don't break these)

1. **Never commit Baofeng's code or anything derived from it.** That includes the stock `.ufw` and anything
   unpacked from it, disassembly, lifted C, AI rewrites of lifted C, and patched images. These all live in
   `firmware/` and `build/`, which are git-ignored. Don't force-add them. Addresses, register values, pin maps and
   descriptions of behaviour are facts and are fine in docs and code comments.
2. **Third-party code only when its licence allows it.** Open source or proprietary (e.g. a chip vendor's SDK or
   libraries) is fine if its licence lets us use, modify as needed, and redistribute it in this repo and in built
   images. Before adding any: read the licence, keep its headers, licence file and NOTICE, and add a row to
   [docs/THIRD-PARTY.md](docs/THIRD-PARTY.md). No licence, unclear licence, or "internal use only" means no. Stock
   firmware code is never in this group: Baofeng grants no licence, so read the lifted C to learn what the hardware
   needs, then write your own implementation; don't paste or translate it.
3. **Never flash anything to a radio for the user, and don't tell them a build is safe to flash.**
   - The C build has never run on a radio.
   - `tools/mkufw.py` refuses images without the stock update mode; don't work around it.
   - A radio running an app that crashes before its update check can't be reflashed with the cable
     ([docs/TESTING.md](docs/TESTING.md)).
4. **Never cross the two kinds.** Nothing made for NRF goes on NRFB, or the other way round. Calibration and
   settings sit in different places.
5. **Transmit only in legal bands.** Frequency changes widen receive; transmit stays limited to the amateur bands
   by default.
6. **The flash areas `PRCT` (calibration) and `VM` (settings) must never be erased** by our code, except the update
   loader's own slot, which the update mode handles.

## Quickstart

Needs: Python 3.10+, git, MSYS2 `make` and a host `gcc` (for tests), and for target builds
[pi32v2-isa](https://github.com/jvitkauskas/pi32v2-isa) cloned next to this repo plus its pi32v2-llvm toolchain.

```
# 1. Stock file: the user downloads it (links in README.md, "Which radio") into firmware/. Don't download it unasked.
python tools/setup.py                                  # checks SHA-256, unpacks into build/stock/ (and build/stock-nrfb/)

# 2. Read the stock code
python tools/disasm.py build/stock/unpacked/app.bin --isa ../pi32v2-isa      # build/app.dis, build/xrefs.txt
python tools/lift/lift.py                                                     # build/lift/lifted.c (~2.5 min first time)

# 3. Build our firmware
make check                                             # host gcc syntax check, no toolchain needed
make TOOLCHAIN=<path>/pi32v2-llvm/bin                  # firmware.bin
make ufw TOOLCHAIN=...                                 # build/open-uv5r-mini.ufw, with the stock loader
```

Features are switched in `features.mk` (CHIRP, the start-up screen test, saving, transmit): 1 in, 0 out, as in
egzumer's UV-K5 firmware. One build without editing it: `make ENABLE_CHIRP=0 TOOLCHAIN=...`. Changing a switch
rebuilds everything that uses it. The full how-to for people: [docs/BUILDING.md](docs/BUILDING.md).

For NRFB: `tools/disasm.py build/stock-nrfb/unpacked/app.bin … -o build/nrfb/app.dis --xrefs build/nrfb/xrefs.txt`,
then `tools/lift/lift.py --variant nrfb`.

## Where things are

| Path | What |
| --- | --- |
| `src/` | All radio firmware. `driver/` chips; `radio/` radio core (`radio.c`), tones, scan, dual watch; `store/` calibration, channels, settings; `app/` keys and main loop (`app.c`); `ui/` screens (`screen_main.c`), font, generated icons; `update/` the cable update mode (safety-critical); `bsp/`, `bt/`; start-up `main.c`, `board.c`, `start.S`, `firmware.ld`. Rules and naming: [docs/LAYOUT.md](docs/LAYOUT.md) |
| `assets/` | Icons as pixel-art text (`assets/icons/<group>_<name>.txt`); `python tools/mkassets.py` makes `src/ui/icons.c`. Never edit the generated C |
| `tools/` | `setup.py`, `jl_ufw_unpack.py`, `disasm.py`, `mkufw.py`, `ufwcheck.py`, `flashcheck.py`, `uv5r_flasher.py` (has an emulated radio), `ufwpatch.py`, `build_matching.py`, `variant_diff.py`, `symbols.txt` (names for stock addresses) |
| `tools/lift/` | Lifter (stock → C), `insn_test.py`, `func_test.py` (checks against the stock code), `local_rewrite.py` (readable C with a local model) |
| `emu/` | CPU and hardware emulator running the real stock app |
| `tests/` | PC tests, e.g. `update_host.py` runs our update mode against the real flasher host |
| `docs/` | Read [ROADMAP](docs/ROADMAP.md) first, then [RE-NOTES](docs/RE-NOTES.md) (pins, chips, findings), [LIFTER](docs/LIFTER.md), [EMULATOR](docs/EMULATOR.md), [UPDATE-MODE](docs/UPDATE-MODE.md), [TESTING](docs/TESTING.md), [FIRMWARE](docs/FIRMWARE.md) |

## Key facts you'll need

- **Addresses.** The stock app runs at `0x01E00100` (flash is mapped at `0x01E00000`). Stock functions are named
  `sub_XXXXXXXX` by address; `tools/symbols.txt` has the known names.
- **Lifted C.** It works on a CPU state `S` (`S.r[0..15]`, `S.sp`, `S.rets`) with `rd8/16/32`, `wr8/16/32` and
  `lift_call(addr)`. Arguments go in `r0..r3` and the result comes back in `r0`.
- **The update mode is entered with PTT-A + `=` at power-on.** The cable UART is routed to the USB pins: RX 61 (D+),
  TX 62 (D−).
- **Pins** (BK4819, LCD, keypad, PTT, power) are in [docs/RE-NOTES.md](docs/RE-NOTES.md). Register-level notes
  for each driver are in the driver's header comment.
- **Flash layout (NRF):**

  | Area | Where |
  | --- | --- |
  | App | flash `0x3100`, ~479 KB, full |
  | `PRCT` (protected, includes calibration) | to `0x94000` |
  | `VM` (settings) | `0x94000`–`0xFE000` |
  | `BTIF` | `0xFE000` |
  | `key_mac` | `0xFF000` |

  NRFB is shifted 4 KB.

## How to work out what the hardware needs

1. Find the stock function: search `build/app.dis` for a string or register, or use `tools/symbols.txt`, or
   `build/lift/functions.json` (calls and strings for each function).
2. Read its lifted C in `build/lift/lifted.c`. If `build/readable/sub_XXXXXXXX.c` exists, it's a tested readable
   version.
3. To see it run: `python emu/uv5r_emu.py --isa ../pi32v2-isa --steps N` (`--watch LO-HI` prints SFR writes with
   the pc). The scripts in `emu/` show how to run one stock routine and record what it writes.
4. Write the facts (pins, register values, sequence) into docs/RE-NOTES.md or the driver comment, citing the stock
   address, e.g. "(stock `sub_01E46F3E`)". Then write our code.

## Checks before you commit

- `make check` (also checks the generated icons are up to date), and `make TOOLCHAIN=…` if you touched firmware code.
- Screen or icons changed: `python tests/ui_preview.py`, then look at `build/ui/*.png`.
- Update mode or flasher changed: `python tests/update_host.py`; UART driver changed: `python tests/uart_host.py`; radio core changed: `python tests/radio_host.py`; stock settings reader or writer changed: `python tests/settings_host.py`; CHIRP programming (`src/app/program.c`) changed: `python tests/chirp_host.py` (needs a CHIRP checkout); flasher guard or update-mode marker changed: `python tests/flasher_guard.py`.
- Lifter changed: `python tools/lift/insn_test.py` (0 must disagree) and
  `python tools/lift/func_test.py --steps 3000000` (all must match).
- `git status`: nothing from `build/` or `firmware/` staged.

## Contributing

- Small commits, one subject each. Messages look like `area: what changed` (e.g. `tools/lift: …`, `update mode: …`,
  `docs: …`). Work on a branch and open a pull request.
- Update the docs in the same change: the ROADMAP row's status, and the findings in RE-NOTES. Docs are short, plain
  English, with a table where it helps.
- Say what was tested and how: on the PC, in the emulator, or on a radio. Never write "works on a radio" unless a
  person tried it on one.
- Good first tasks: any **todo** or **unknown** row in [docs/ROADMAP.md](docs/ROADMAP.md). Section 2 (platform)
  and 3 (radio) unblock the most.

## Pitfalls seen so far

- Windows with Git Bash: in Python heredocs, `\\n` and `\\d` get turned into the wrong characters. Write scripts to a
  file instead.
- Long steps: a first disassembly or lift takes ~2 minutes, and `func_test.py` several. Run them in the background
  rather than in a loop.
- `build/lift/lifted.c` is ~20 MB. Search it; don't read it whole.
- Don't leave a bare `python -` (or any command reading stdin) running: it waits for ever.
