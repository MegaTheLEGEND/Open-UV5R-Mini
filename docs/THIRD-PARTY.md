# Third-party code

Code we did not write is allowed when its licence lets us use it, change it where needed, and redistribute it, in
this repo and in the images we build. That can be open source or proprietary (a chip vendor's SDK or libraries).

Before adding any:

1. Read its licence. No licence, an unclear one, or "internal use only" / "evaluation only" means no.
2. Check it fits with the other code in the image (for example, GPL code can't be linked with proprietary libraries).
3. Keep its copyright headers, licence file and NOTICE; put them next to the code.
4. Add a row below.

Never in this group: Baofeng's stock firmware or anything derived from it (no licence is granted; AGENTS.md rule 1).

| Code | Where in the repo | Licence | Source | Used for |
| --- | --- | --- | --- | --- |
| pi32v2-llvm (clang, ld.lld, llvm-objcopy) built as WebAssembly, its C headers and compiler-rt builtins | not in the repo: `web/toolchain/build-wasm.sh` builds it; published on the web builder site (`build/site/toolchain/`, with `LICENSE.TXT`) | Apache-2.0 with LLVM Exceptions | [jvitkauskas/pi32v2-llvm](https://github.com/jvitkauskas/pi32v2-llvm) tag `pi32v2-23.1.6-2`, plus [YoWASP](https://github.com/YoWASP/llvm-project) commit 97196c8 (WASI patch, downloaded by the script) and our fix-ups in `web/toolchain/*.pl` | the in-browser build ([WEB-FLASHER.md](WEB-FLASHER.md)). The JieLi libraries below are also published on that site, with their `LICENSE` |
| JieLi BR23 libraries `cpu.a` (FM receiver: `fm_inside_api`, `fm_init`, `fm_rf`, `fm_scan`, `fm_digital_set`, `fm_global_dat`, `fm_dem`; helpers `clock`, `efuse`, `p33`) and `media.a` (`audio_dac`) | not in the repo: `tools/sdk/fm_libs.sh` copies them from your SDK clone into `build/sdk/` with the SDK's `LICENSE`; linked into images built with `ENABLE_APP_FM` | Apache-2.0 (the SDK's `LICENSE`) | [fw-AC63_BT_SDK](https://github.com/Jieli-Tech/fw-AC63_BT_SDK) `cpu/br23/liba/`, tested at commit fdc018d | the FM radio (ROADMAP 3.9). Unchanged; our own glue around them in `src/platform/jl_sdk/` |
| JieLi BR23 library `btctrler.a` (the Bluetooth controller: BLE link layer, RF, analog) | not in the repo: `tools/sdk/fm_libs.sh` copies it from your SDK clone into `build/sdk/` with the SDK's `LICENSE`; linked into images built with `ENABLE_BT_LE` (only the BLE parts stay after link-time optimisation, about 25 KB) | Apache-2.0 (the SDK's `LICENSE`) | [fw-AC63_BT_SDK](https://github.com/Jieli-Tech/fw-AC63_BT_SDK) `cpu/br23/liba/`, tested at commit fdc018d | BLE scanning, advertising and one connection for the serial link (ROADMAP 5.5). Unchanged; our own glue in `src/platform/jl_sdk/bt_le.c`, `bt_glue.c`; the GATT server on top is our own (`src/bt/gatt.c`) |

Candidates, licence still to check: UV-K5 open firmware (DualTachyon, egzumer; Apache-2.0 per their repos), FreeRTOS
(MIT).

**JieLi fw-AC63_BT_SDK** (checked 2026-11-23, not added yet): <https://github.com/Jieli-Tech/fw-AC63_BT_SDK>. The
repository's only licence is Apache-2.0 (`LICENSE`, copyright line not filled in); its README says the sources "must
be combined with lib.a" and sets no other terms for the binaries. It has the AC695N's chip family (BR23) in
`cpu/br23/`: register headers, drivers (`spi.c`, …) and closed libraries in `cpu/br23/liba/` (`btctrler.a` Bluetooth
controller, `btstack.a` host, `system.a` OS, `cpu.a`, `media.a`, …). So those may be used, changed where their source
is given, and redistributed under Apache-2.0 (keep `LICENSE`, state changes). What we use so far: register facts
only (SPI1, IO map, FM register names; RE-NOTES), no code. The Bluetooth edition ([BLUETOOTH.md](BLUETOOTH.md)) would
link the libraries; a row goes in the table when they are added. `tools/sdk/` (2026-11-24) builds the SDK from a
clone outside this repo and changes that clone with a few `sed` edits (Apache-2.0 allows changes; they are listed
in `tools/sdk/README.md`); no SDK file is copied here. `tools/sdk/libc/` is our own code. Note: the libraries are closed binaries, so an image
that contains them is not fully open source, and Bluetooth products may need qualification where they are sold.
