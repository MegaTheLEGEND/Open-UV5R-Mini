# Web builder and flasher

A web page that builds the firmware in your browser with the `features.mk` switches you pick, puts it into your own
stock file, checks it, and flashes it over the K1 cable with Web Serial. Nothing is uploaded: the compiler runs in
the page. **No build is known to be safe to flash** (read [TESTING.md](TESTING.md) first).

## Use it

1. Open the page in **Chrome or Edge** on a desktop computer (other browsers can build but not flash).
2. **Your radio**: hold `8` while switching on. Only `5RMINI NRF` (stock V0.27) is supported; the page refuses NRFB.
3. Load your two files (see [BUILDING.md](BUILDING.md) section 2): `stock-nrf-v0.27.ufw` (its SHA-256 is checked)
   and MM9PDY's to-stock patch (adds the update loader). They stay on your computer.
4. **Features**: tick the switches. The page shows the same `make` command for a PC build.
5. **Build**. The first time it downloads the compiler (about 21 MB, kept by the browser); a build takes a few
   seconds. Then the page combines it with your files and shows every check (the same as
   `tools/ufwcheck.py` and `tools/uv5r_flasher.py`). You can download `firmware.bin` or the `.ufw`.
6. **Flash**: tick that you've read the risks, and (our update mode is not proven on a radio) that you can open
   the case and recover the radio with the ROM rescue mode; press Flash, pick the cable's port, then switch the radio on with **PTT-A** + `=` held. If an update
   stops part-way: switch the radio off and on with no keys and press Flash again; the loader carries on.

## What the site holds, and what it doesn't

| On the site | Licence |
| --- | --- |
| The page (`web/`) and the firmware sources at the commit it was made from | ours |
| `llvm.wasm.gz`: pi32v2-llvm's clang, ld.lld and llvm-objcopy as one WebAssembly program, the compiler's C headers and compiler-rt's builtins | Apache-2.0 with LLVM Exceptions |
| JieLi's `cpu.a`, `media.a`, `btctrler.a` with their `LICENSE` | Apache-2.0 ([THIRD-PARTY.md](THIRD-PARTY.md)) |

Never on the site: the stock `.ufw`, MM9PDY's patch, or anything made from them. The page builds the flashable
`.ufw` in the browser from the user's own files, the way `make ufw` does.

The page keeps the rules of the Python tools: the stock file must be exactly stock-nrf-v0.27; the app must carry
an update mode (the `mkufw.py` guard); before flashing, the container, CRCs, pinned loader, flash area, stock bytes
outside the app window and the update mode are checked (`check_image`), and the radio is only sent bytes of that
image that it asked for. Transmit is limited by the firmware itself (TX LOCK = HAM by default).

## How it works

| Part | File |
| --- | --- |
| Switches → the compile and link commands `make` would run | `web/lib/plan.js` (port of the Makefile and `features.mk`) |
| WASI runtime with an in-memory file system | `web/lib/wasi.js` |
| One compile or the link; a pool of Web Workers | `web/lib/buildcore.js`, `build-worker.js`, `build.js` |
| `.ufw` tools: patch, mkufw with its guard, ufwcheck, image checks | `web/lib/ufw.js` (port of `tools/ufwpatch.py`, `mkufw.py`, `ufwcheck.py`, `uv5r_flasher.py`) |
| The cable update session over Web Serial | `web/lib/flasher.js` (port of `flash_radio` in `tools/uv5r_flasher.py`) |
| The toolchain as WebAssembly | `web/toolchain/build-wasm.sh`: pi32v2-llvm cross-built with wasi-sdk, with YoWASP's WASI patch and our small fix-ups (`*-fixups.pl`); about an hour on 24 cores |
| The site | `python web/tools/mksite.py` → `build/site/` |
| Publishing | `.github/workflows/pages.yml`: on every push to `main` or by hand; needs Settings → Pages → Source = GitHub Actions once. The first run builds `llvm.wasm` (about two hours), later runs take it from the cache |
| Releases | `.github/workflows/release.yml`: push a tag `v*`; makes a draft release with `firmware.bin` for four switch sets and SHA-256 sums (no `.ufw`) |

## Make and check it on the PC

```sh
sh web/toolchain/build-wasm.sh ~/wasm                      # WSL or Linux, once per LLVM version
python web/tools/mksite.py --toolchain-wasm <~/wasm/out> --llvm-release <unpacked pi32v2-llvm linux release>
python -m http.server 8770 -d build/site                   # then open http://127.0.0.1:8770
```

| Test | Checks |
| --- | --- |
| `python tests/web_plan_host.py` | the page's commands equal `make -n`'s for 40 switch sets |
| `python tests/web_build_host.py --toolchain build/llvm-release/bin --wsl` | `firmware.bin` built with `llvm.wasm` (in Node) is byte for byte the native build, 3 switch sets |
| `python tests/web_ufw_host.py` | the JS `.ufw` equals `tools/mkufw.py`'s byte for byte, passes `tools/ufwcheck.py`; NRFB stock and an app without update mode are refused |
| `python tests/web_flasher_host.py` | the JS flasher against the emulated radio of `tools/uv5r_flasher.py`: a whole update, and a power cut at 50 % then resumed |

Tested on the PC (2026-11-27): all four pass; in Edge's engine (the Claude app's browser pane) the page built the
default and a no-FM/no-TX switch set in about 3 s each, byte for byte the native builds, and combined and checked
the `.ufw`. **Not tested: flashing a radio from the page** (Web Serial to a real K1 cable).

## Limits

- NRF only, like `make ufw`.
- The toolchain version is the one `llvm.wasm` was built from (`LLVM_TAG` in `build-wasm.sh`, now
  pi32v2-23.1.6-2); the native release you build with may differ.
- Changing the Makefile's source list or `features.mk`'s flags needs the same change in `web/lib/plan.js`;
  `tests/web_plan_host.py` catches a difference.
