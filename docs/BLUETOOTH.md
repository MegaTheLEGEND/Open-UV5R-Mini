# Bluetooth (optional edition)

The UV-5R Mini's Bluetooth (used by the stock firmware for **Wireless CPS**) runs on the AC695N's built-in
Bluetooth radio. JieLi only ships its Bluetooth controller and stack as closed, precompiled libraries
(`btctrler.a`, `btstack.a`), so this project offers two builds:

| Build | Bluetooth | Contains closed code | How |
| --- | --- | --- | --- |
| **Open** (default) | no | no | `make` in this repo, bare metal (`src/main.c`, `src/bt/bt_none.c`) |
| **Bluetooth edition** | yes (SPP serial port) | yes, JieLi's libraries from **your own** SDK copy | the radio app built as a task inside JieLi's AC695N SDK |

**Update 2026-11-23: where the SDK comes from.** JieLi publishes an SDK for this chip family under Apache-2.0:
[fw-AC63_BT_SDK](https://github.com/Jieli-Tech/fw-AC63_BT_SDK) has `cpu/br23/` (BR23 = the AC695N's family) with
the closed libraries the Bluetooth edition needs (`cpu/br23/liba/btctrler.a`, `btstack.a`, `system.a`, `cpu.a`,
`media.a`) and a BR23 board for its `spp_and_le` app (SPP and BLE data). The licence check is in
[THIRD-PARTY.md](THIRD-PARTY.md). So "your own SDK copy" below can be that public repository, cloned next to this
one. The same SDK has the AC695N's audio DAC driver (`media.a`, `asm/dac.h`), which the FM broadcast receiver needs
too (RE-NOTES, "FM receiver, traced"): the SDK edition is the natural home for both Bluetooth and FM.

**Update 2026-11-24: the SDK builds with open tools.** Its `ac635n_spp_and_le` app (BR23 board) compiles and links
with pi32v2-llvm through the drop-in, using `tools/sdk/build_sdk.sh`, our own small C library and five small fixes
([tools/sdk/README.md](../tools/sdk/README.md)). The result is an `sdk.elf` and a 141 KB `app.bin`. Not yet: a
firmware package (the SDK's packaging step needs JieLi-only tools), a check that link-time optimisation kept
everything the Bluetooth libraries need, and any run on a chip.

This repository never contains JieLi code or libraries. The Bluetooth edition is built locally from an SDK
copy you supply, and the resulting firmware must not be redistributed unless JieLi's terms allow it.

## Why it is a separate build

The Bluetooth libraries depend on the rest of JieLi's runtime: the SDK links them together with
`cpu.a`, `system.a` (its OS: tasks, timers, interrupts, memory), `media.a` and `aes_lib_pi32.a`, using the
SDK's own linker script (`cpu/br23/sdk.ld`). So in the Bluetooth edition the radio code runs as one task of
JieLi's OS instead of on bare metal. The radio code itself is the same: `app/`, `src/driver/`, `src/ui/` are shared,
and `src/bt/bt.h` is the only interface between them and Bluetooth.

## Building it (no JieLi tools)

You need:

1. An AC695N (BR23) SDK tree, for example a copy of JieLi's `AC695N_soundbox` SDK.
2. The open pi32v2 LLVM toolchain and the **drop-in** from
   [jvitkauskas/pi32v2-isa](https://github.com/jvitkauskas/pi32v2-isa) (`docs/DROPIN.md`), which runs the SDK's
   own Makefiles with clang and ld.lld instead of JieLi's compiler. Shown on the AC79 SDK by its author; the BR23
   SDK links with it plus `tools/sdk/` (2026-11-24, above).

Steps:

1. In the SDK's application Makefile (`apps/soundbox/Makefile`), add this repository's shared sources and the
   two platform files to the C source list:
   `src/app/app.c src/board.c src/driver/*.c src/ui/*.c src/platform/jl_sdk/bt_spp.c src/platform/jl_sdk/radio_task.c`,
   and this repository's root to the include path. Do **not** add `src/main.c`, `src/start.S` or `src/bt/bt_none.c`.
2. Choose a board configuration in the SDK with no LCD, keys or ADC keys enabled, so the SDK does not drive the
   pins our drivers own (PA4, PA6–PA12, PB0–PB11, PC0–PC5; see `src/board.h`).
3. Enable the SPP profile in the SDK's Bluetooth configuration.
4. Call `open_uv5r_start()` once from the SDK application's init, after Bluetooth is started.
5. Build with the drop-in toolchain, then package with the SDK's own tools or this repo's packer (not written yet).

## Status

| Part | State |
| --- | --- |
| `src/bt/bt.h` interface, open stubs | done |
| SPP glue (`src/platform/jl_sdk/bt_spp.c`) | written against the SDK's `spp_user.h`; not compiled yet |
| OS task glue (`src/platform/jl_sdk/radio_task.c`) | written against the SDK's `os_api.h`; not compiled yet |
| Key-test app | shows the Bluetooth state and echoes received bytes back (loopback test) |
| Wireless CPS protocol | **the cable programming protocol over SPP** (stock code read 2026-11-26, below); our `src/app/program.c` already speaks it |
| BR23 SDK build with open tools | `tools/sdk/build_sdk.sh`: `ac635n_spp_and_le` links (`sdk.elf`, `app.bin`); no `.ufw`, not run (2026-11-24) |
| SDK integration script | not written; steps above are manual |

## Stock's Wireless CPS, read from its code (2026-11-26)

Facts from the stock app (NRF V0.27), not tried with a phone or PC yet.

| What | Stock | Where |
| --- | --- | --- |
| Services it advertises (SDP records) | SPP (UUID `0x1101`) on RFCOMM channel 2, name `JL_SPP`; Device ID (`0x1200`: vendor `0x05D6` JieLi, product `0x000A`, version `0x0240`); AVRCP (`0x110E`, `0x110F`, `0x110C`) | data at `0x01E0AEC4`-`0x01E0B0CC` |
| Bytes received over Bluetooth | a callback with the event number in its second argument (events 6-35); **event 34 = data**: the bytes are copied into the same command buffer the cable uses (RAM `0x198C0`+3553, length at +3548) and checked for `PROGRAMCOLORPROU` (`sub_01E3DFF0`), then programming mode starts as for the cable (state bytes +2781 = 8, +2782 = 1) and the flag at +2876 is set to 1 ("link is Bluetooth") | `0x01E41DD6` (data path `0x01E41EC8`) |
| Bytes received on the cable | UART events `URF` / `URT` (`0x55524600` / `0x55525400`: buffer full, time-out) read up to 2048 bytes into `0x18134`, then `sub_01E4C940` appends them (1100-byte staging buffer at +8008); the main loop moves them into the command buffer only while the flag at +2876 is 0 | `0x01E54F60`, `sub_01E52FAE` |
| Replies | one function for both: `sub_01E4CA10(data, len)` sends over Bluetooth (`sub_01E3DECE(34, data, len, 0)`, after a ready check `sub_01E3DFCC`) when +2876 is set, else to the UART | `0x01E4CA10` |
| The protocol itself | the same command handler as the cable (`sub_01E4CCDA`): "PROGRAMCOLORPROU", "F", "M", reads, writes, "E" | as `src/app/program.c` |

So for Wireless CPS the Bluetooth edition needs only: SPP named like stock's, its data handed to `program.c`'s
receive side, and `program.c`'s replies sent back over SPP instead of the UART (`src/platform/jl_sdk/bt_spp.c`).
The command number 34 in `sub_01E3DECE` does not match the public SDK's `USER_CTRL_SPP_SEND_DATA` (127), so stock
was built from a different SDK version; our build uses the public SDK's own call.

## What Bluetooth could do on this radio (2026-11-24)

The AC695N has Bluetooth 5 (classic and BLE) and JieLi's public SDK has its stack. What each idea needs:

| Idea | Profile | Needs | Effort |
| --- | --- | --- | --- |
| CHIRP over Bluetooth: the PC sees a Bluetooth serial port, CHIRP talks our cable protocol (`src/app/program.c`) to it unchanged | classic SPP | the Bluetooth edition; SPP glue exists (`bt_spp.c`) | medium |
| Phone app: change channel or frequency, see the S-meter and spectrum, edit channels | BLE, one data service | a phone app (web Bluetooth would do) and a small protocol | medium |
| APRS / packet with a phone app (APRSdroid and others speak KISS over Bluetooth, as Mobilinkd TNCs) | SPP, KISS | an AFSK 1200 modem on the BK4819 (ROADMAP 7.x), then KISS framing | large |
| The clock set from the phone (ROADMAP 4.7) and GPS position for APRS beacons | BLE or SPP | the phone app above | small, after it |
| Remote PTT button / Bluetooth PTT key | BLE HID (the radio as host) | host-side HID in JieLi's stack; unclear it supports that | unknown |
| Bluetooth speaker: music from a phone through the radio's speaker | A2DP sink | the DAC path (working once the FM radio plays), the SBC decoder (in `media.a`), and room: the full stack is about 150-250 KB | large |
| Headset for the radio (hear receive and talk on transmit through Bluetooth earbuds) | HFP / A2DP source | receive audio into the MCU and the MCU's audio out to the BK4819's mic input: stock never feeds radio audio through the MCU, and the board wiring for it is not known (RE-NOTES "Audio") | probably not possible without board changes |
| Firmware updates over Bluetooth | SPP | a safe update path; a crash there can't be fixed by cable | not planned |

All of them need the Bluetooth edition: JieLi's controller and stack are closed libraries that expect JieLi's OS (tasks, timers). Since 2026-11-24 the FM radio shows JieLi's libraries can link into our bare-metal build with a small glue, but the Bluetooth stack leans on the OS far more than the FM library does, so the plan is still to run our radio code as a task inside the SDK.

## A smaller option: BLE only (idea, not started)

To control the radio from a phone you only need Bluetooth Low Energy with one data service, like a wireless serial
cable. Calls (HFP), music (A2DP) and classic pairing make up most of the size of JieLi's stack, and none of them
are needed for that.

- **Controller** (the radio packets): JieLi's own, part in ROM and part in its `btctrler` library (the name is in the
  stock firmware). It has to come from JieLi.
- **Host** (pairing, connections, the data service): small open ones exist. NimBLE (Apache-2.0, Apache Mynewt) is
  about 50 KB in a minimal setup. BTstack is small too, but free only for non-commercial use.
- **The open question:** does JieLi's controller use the standard host interface (HCI)? If it does, NimBLE on top
  could make Bluetooth control about 60–100 KB. If not, JieLi's SDK can build BLE-only, which also leaves the
  audio profiles out. To find out, trace how the stock code calls the `btctrler` functions (lifted C,
  [LIFTER.md](LIFTER.md)).

Rough sizes: full stock-style Bluetooth about 150–250 KB; BLE-only about 60–100 KB. Both are estimates.
