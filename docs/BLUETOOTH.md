# Bluetooth (optional edition)

The UV-5R Mini's Bluetooth (used by the stock firmware for **Wireless CPS**) runs on the AC695N's built-in
Bluetooth radio. JieLi only ships its Bluetooth controller and stack as closed, precompiled libraries
(`btctrler.a`, `btstack.a`), so this project offers two builds:

| Build | Bluetooth | Contains closed code | How |
| --- | --- | --- | --- |
| **Open** (default) | no | no | `make` in this repo, bare metal (`main.c`, `bt/bt_none.c`) |
| **Bluetooth edition** | yes (SPP serial port) | yes, JieLi's libraries from **your own** SDK copy | the radio app built as a task inside JieLi's AC695N SDK |

This repository never contains JieLi code or libraries. The Bluetooth edition is built locally from an SDK
copy you supply, and the resulting firmware must not be redistributed unless JieLi's terms allow it.

## Why it is a separate build

The Bluetooth libraries depend on the rest of JieLi's runtime: the SDK links them together with
`cpu.a`, `system.a` (its OS: tasks, timers, interrupts, memory), `media.a` and `aes_lib_pi32.a`, using the
SDK's own linker script (`cpu/br23/sdk.ld`). So in the Bluetooth edition the radio code runs as one task of
JieLi's OS instead of on bare metal. The radio code itself is the same: `app/`, `driver/`, `ui/` are shared,
and `bt/bt.h` is the only interface between them and Bluetooth.

## Building it (no JieLi tools)

You need:

1. An AC695N (BR23) SDK tree, for example a copy of JieLi's `AC695N_soundbox` SDK.
2. The open pi32v2 LLVM toolchain and the **drop-in** from
   [jvitkauskas/pi32v2-isa](https://github.com/jvitkauskas/pi32v2-isa) (`docs/DROPIN.md`), which runs the SDK's
   own Makefiles with clang and ld.lld instead of JieLi's compiler. It has been shown on the AC79 SDK; the
   BR23 SDK is untested.

Steps:

1. In the SDK's application Makefile (`apps/soundbox/Makefile`), add this repository's shared sources and the
   two platform files to the C source list:
   `app/app.c board.c driver/*.c ui/*.c platform/jl_sdk/bt_spp.c platform/jl_sdk/radio_task.c`,
   and this repository's root to the include path. Do **not** add `main.c`, `start.S` or `bt/bt_none.c`.
2. Choose a board configuration in the SDK with no LCD, keys or ADC keys enabled, so the SDK does not drive the
   pins our drivers own (PA4, PA6–PA12, PB0–PB11, PC0–PC5; see `board.h`).
3. Enable the SPP profile in the SDK's Bluetooth configuration.
4. Call `open_uv5r_start()` once from the SDK application's init, after Bluetooth is started.
5. Build with the drop-in toolchain, then package with the SDK's own tools or this repo's packer (not written yet).

## Status

| Part | State |
| --- | --- |
| `bt/bt.h` interface, open stubs | done |
| SPP glue (`platform/jl_sdk/bt_spp.c`) | written against the SDK's `spp_user.h`; not compiled yet |
| OS task glue (`platform/jl_sdk/radio_task.c`) | written against the SDK's `os_api.h`; not compiled yet |
| Key-test app | shows the Bluetooth state and echoes received bytes back (loopback test) |
| Wireless CPS protocol | unknown; needs the stock firmware's CPS handler reverse-engineered or a capture from the stock app |
| SDK integration script | not written; steps above are manual |
