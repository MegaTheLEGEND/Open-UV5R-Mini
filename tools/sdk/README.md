# Building JieLi's BR23 SDK with open tools

The Bluetooth and FM "SDK edition" ([docs/BLUETOOTH.md](../../docs/BLUETOOTH.md)) is built on JieLi's public SDK for
the AC695N's chip family, [fw-AC63_BT_SDK](https://github.com/Jieli-Tech/fw-AC63_BT_SDK) (Apache-2.0, `cpu/br23`).
Its Makefiles expect JieLi's compiler. This folder builds them with the open pi32v2-llvm toolchain instead, through
the drop-in from [pi32v2-isa](https://github.com/jvitkauskas/pi32v2-isa) (`docs/DROPIN.md`), plus what the BR23 SDK
needs on top.

Linux or WSL. Nothing from the SDK goes into this repo; the SDK clone is changed in place, outside it.

## Recipe

```sh
# 1. The Linux pi32v2-llvm release, e.g. pi32v2-llvm-23.1.6-2-linux-x64 from jvitkauskas/pi32v2-llvm, unpacked
L=~/pi32v2/pi32v2-llvm-23.1.6-2-linux-x64
# 2. The drop-in's two runtime libraries (no LLVM source checkout needed)
mkdir -p ~/pi32v2/rt/rt
cp $L/lib/clang/23/lib/pi32v2/libclang_rt.builtins.a ~/pi32v2/rt/libclang_rt.builtins-pi32v2.a
$L/bin/clang --target=pi32v2 -O2 -ffreestanding -c pi32v2-isa/tools/llvm/runtime/sync.c -o ~/pi32v2/rt/rt/sync.o
$L/bin/llvm-ar rcs ~/pi32v2/rt/libpi32v2.a ~/pi32v2/rt/rt/sync.o
# 3. The drop-in (pi32v2-isa cloned with Unix line endings: a Windows checkout's CRLF breaks its shell scripts)
sh pi32v2-isa/tools/dropin/make_dropin.sh $L/bin ~/pi32v2/rt ~/pi32v2/jieli
# 4. The SDK, with Unix line endings
git -c core.autocrlf=false clone https://github.com/Jieli-Tech/fw-AC63_BT_SDK ~/pi32v2/sdk
# 5. Build (installs the C library below into the drop-in, applies the SDK fixes, runs the SDK's make)
sh tools/sdk/build_sdk.sh ~/pi32v2/sdk ~/pi32v2/jieli            # default target ac635n_spp_and_le
#    ... or with our radio code as a task of the app (tools/sdk/add_radio.sh; docs/BLUETOOTH.md)
sh tools/sdk/build_sdk.sh --radio ~/pi32v2/sdk ~/pi32v2/jieli
# 6. Section images and app.bin from the ELF
sh pi32v2-isa/tools/dropin/postbuild.sh ~/pi32v2/jieli/pi32v2/bin ~/pi32v2/sdk/cpu/br23/tools/sdk.elf OUTDIR
```

## Result (2026-11-24)

`ac635n_spp_and_le` (`apps/spp_and_le`, board br23, `CONFIG_APP_SPP_LE`) compiles and **links**: `sdk.elf`,
entry `_start` at `0x01E00120`, about 108 KB of code and 32 KB of data; `postbuild.sh` makes a 141 KB `app.bin`.
A clean build from a fresh clone takes about 5 s on 24 cores.

The SDK's own post-build step (`cpu/br23/tools/download.sh`) then stops: it needs JieLi-only tools
(`/opt/utils/strip-ini`, `remove_tailing_zeros`, `lz4_packet`, `host-client`) and runs from the wrong directory. It
stops the same way with JieLi's own Linux tools (pi32v2-isa `docs/DROPIN.md`). `build_sdk.sh` counts that as a
success when `sdk.elf` exists. So **no firmware package** (`.ufw`) is made, and nothing has run on a chip.

Not checked yet (and see docs/BLUETOOTH.md, "From `sdk.elf` to a flashable `.ufw`": with ld.lld the SDK's
`.ld` fragments make several `.text`/`.data` sections, and `data_begin` points at the end of the first one, so
this image would not start as it is):

- **Whether everything needed was kept.** JieLi's libraries are LLVM bitcode, so link-time optimisation decides what
  stays. 116 of the 1961 functions `btctrler.a` defines and 46 of `btstack.a`'s 1159 have a symbol in the ELF.
  Inlining explains part of that; code reached only through tables the SDK finds by section
  (`sdk_used_list.used`, passed on as `--undefined-glob`) could also have been dropped. A JieLi-built `sdk.elf` of
  the same app to compare with, or a run in the emulator or on a radio, would settle it.
- **Behaviour.** Code generation is pi32v2-llvm's (see the drop-in's notes for where it differs from JieLi's), and
  the fixes below change a few details.

## SDK fixes (`build_sdk.sh`)

Each one is for a difference between JieLi's tools and the open ones, applied with `sed` to the SDK clone.

| # | What | Why |
| --- | --- | --- |
| 1 | Tuya's `tuya_ble_type.h`: `register unsigned sp __asm("sp")` becomes `__builtin_frame_address(0)` | pi32v2-llvm has no named `sp` register. Tuya uses it for a stack-depth debug print |
| 2 | `--plugin-opt=-warn-stack-size=256` and `--dont-complain-call-overflow` removed from `apps/*/board/br23/Makefile` | neither the drop-in's wrapper nor `ld.lld` knows them; one sets a warning level, the other hides call-range errors |
| 3 | `SUBALIGN(4)` removed from `OVERLAY` (`cpu/br23/sdk_ld.c`, `app_overlay.ld`) | `ld.lld` takes `OVERLAY` but not `SUBALIGN` on it. The overlay start is `ALIGN(4)` and input sections keep their alignment |
| 4 | `:ALIGN(4)` becomes `: ALIGN(4)` (`app.ld`, `update.ld`, `driver_lib.ld`) | `ld.lld` reads `:ALIGN` as one word |
| 5 | `--wrap=exception_analyze` and `fixups/fixups.c` | JieLi's prebuilt `startup.S` (RAM, address 0…) makes a short call (±2 MB) to `exception_analyze` (flash, `0x01E…`). The wrapper sits in RAM code and makes the long call on. No SDK file changes |
| – | `-mllvm=-pi32v2-large-program=true` to ld (`PI32V2_DROPIN_LDFLAGS`) | calls between flash and RAM code need the 6-byte long call; the AC79 SDK passes this to the compiler, the BR23 Makefile doesn't, and with LTO the code is made at link time |

The call-range errors behind fix 2 are real: without the long calls, `ld.lld` stops at its 20-error limit on calls
between RAM and flash code, and with them only fix 5's call is left. How JieLi's toolchain handles these (it is told
not to complain) is unknown.

## The small C library (`libc/`)

The BR23 SDK takes its C library from the compiler's folders (`pi32v2/include`, `pi32v2/lib/r3/libc.a`, `libm.a`);
the drop-in has none, and the AC79 SDK brings its own newlib, this one doesn't. What it needs is small:

- the chip's **mask ROM** has `memcpy`, `memmove`, `memcmp`, `memset`, `memmem`, `strcmp`, `strcpy`, `strlen`,
  `strncmp`, `strstr` (`cpu/br23/maskrom_stubs.ld`);
- the **SDK** has `malloc` and friends, and the `printf` family (`include_lib/system/generic/printf.h`);
- JieLi's closed libraries need only `ceil`, `exp`, `fabsf`, `strcat`, `strchr`, `strncpy`, `strrchr` beyond those;
  the SDK sources add `sprintf`-style calls, `pow`, `floor`, `sqrt`, `rand`, `strtol` and similar.

So `libc/` is our own code (no third-party source): headers `string.h`, `strings.h`, `stdlib.h`, `stdio.h`,
`math.h`, `ctype.h`, `assert.h`, `errno.h`, `time.h`, `unistd.h`, `sys/types.h`, and `string.c`, `stdlib.c`
(strto*, qsort, bsearch, rand, …), `time.c` (UTC calendar only), `math.c` (double and float: floor, sqrt, exp, log,
pow, sin, cos, atan, …; plain series, not tuned for speed). `stdio.h` declares the SDK's `printf` family with the
SDK's own types (size as `unsigned long`). `install_libc.sh` builds it into the drop-in, with the fix-ups in their
own archive (`libopenuv5r_sdkfix.a`).

Tested on a PC: `python tests/sdk_libc_host.py` checks it against the host's C library (strings, strto* edge cases
and errno, strtod, qsort/bsearch, math on a spread of inputs to 1e-14 relative or better, gmtime/mktime): 495 checks.

## Worth reporting to pi32v2-isa / pi32v2-llvm

- the drop-in's `jlwrap.py` doesn't know `--plugin-opt=-warn-stack-size=N` or `--dont-complain-call-overflow`;
- no named `sp` register for `register ... __asm("sp")`;
- `ld.lld`: `SUBALIGN` on `OVERLAY`, and `:ALIGN(` without a space;
- the BR23 SDK needs a C library from the toolchain (this folder's, or a port of picolibc/newlib).
