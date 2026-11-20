# Matching build: stock v0.27 rebuilt from source

The fastest route to firmware that is functionally identical to stock v0.27 is the one decompilation projects use:
turn the stock `app.bin` into assembly source that the **open** pi32v2 toolchain (clang + ld.lld) rebuilds
**byte for byte**. Identical bytes are identical behaviour, including the stock update mode, so this build is the
safe base for testing on a radio and for replacing functions with C one at a time.

## Result (2026-11-19)

| Step | Output | Check |
| --- | --- | --- |
| Source generated from stock `app.bin` (`tools/mkasm.py`) | `build/stock/app.S`: 182,531 positions, 152,391 as instruction text, 30,140 as `.byte` | each text line assembles to the stock bytes |
| clang + ld.lld (`matching/stock.ld`) | `build/stock/app.rebuilt.bin` | **identical** to stock `app.bin` (490,184 bytes) |
| `tools/mkufw.py` | `build/stock-rebuilt-v0.27.ufw` | **identical** to `stock-nrf-v0.27.ufw`, sha256 `811b4b37…9128` |
| + pinned L2 loader (MM9PDY to-stock patch 1.1.0) | `build/open-uv5r-mini-nrf-v0.27-flashable.ufw` | **identical** to the published to-stock image, sha256 `adaef30b…5670` |
| `tools/flashcheck.py` | | the flasher's image checks pass |

## Build it

You need `firmware/stock-nrf-v0.27.ufw` (sha256 `811b4b37253e0685c7efbd3ff0a24aac2bcad223e5453d95b5bfc3f42c089128`),
the MM9PDY to-stock patch in `firmware/` (download link printed by the script if missing; use it unmodified, it is
licensed for that), the pi32v2-llvm toolchain and a pi32v2-isa checkout.

```powershell
python tools/build_matching.py --toolchain E:/toolchains/pi32v2-llvm/bin --isa E:/pi32v2-isa
```

About 2 minutes. It stops at the first step that does not match.

## Change it

Copy `build/stock/app.S`, edit the copy, and build it with `--source` and a build name:

```powershell
python tools/build_matching.py --toolchain E:/toolchains/pi32v2-llvm/bin --isa E:/pi32v2-isa --source build/stock/app-mine.S --name open-uv5r-mine
```

Tested example (`open-uv5r-demo1`): the hold-8 start-up string `5RMINI` changed to `OPENUV`. The rebuilt `app.bin`
differs from stock in exactly those 6 bytes, and the flashable image passes every check.

Rules while the code is still at fixed addresses:

- Keep every instruction and data item at its address: replace bytes with the same number of bytes. Inserting or
  removing bytes shifts everything after it, and the branches are still numeric offsets.
- Named functions come from `tools/symbols.txt`; add names there as functions are identified.
- Keep the stock update mode working (`mkufw.py` refuses a flashable image whose app lacks it).

## Why `.byte` lines

A line stays `.byte` when its text does not assemble back to the same bytes: data in the code (strings, tables),
`if`/`rep` block openers and parallel pairs (their bodies are separate lines), encodings JieLi's compiler chose that
are not the shortest, and words no instruction decodes. Turning these into labelled data and blocks, and branch
offsets into labels, is what makes the source relocatable; that is the next step before larger C replacements.

## Not committed

`build/stock/app.S` and every image are derived from Baofeng's firmware and stay in `build/` (ignored). Only the
tools and `tools/symbols.txt` (names and addresses) are in git.
