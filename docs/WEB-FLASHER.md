# Web builder and flasher

A web page that builds the firmware in your browser with the `features.mk` switches you pick, puts it into your own
stock file, checks it, and flashes it over the K1 cable with Web Serial. Nothing is uploaded: the compiler runs in
the page. **No build is known to be safe to flash** (read [TESTING.md](TESTING.md) first).

## Use it

1. Open the page in **Chrome or Edge** on a desktop computer (other browsers can build but not flash).
2. **Your radio**: hold `8` while switching on. Only `5RMINI NRF` (stock V0.27) is supported; the page refuses NRFB.
3. The page downloads the two outside files into your browser by itself: `stock-nrf-v0.27.ufw` from its MEGA link
   (decrypted in the page with the link's key) and MM9PDY's to-stock patch (it adds back the update loader the
   stock file lacks) from his GitHub repo at a pinned commit. Each must match its pinned SHA-256. If a download
   fails, load your own copies (see [BUILDING.md](BUILDING.md) section 2).
4. **Version and features**: pick a release, a branch's newest commit or a recent commit, or type any commit hash,
   branch or tag. The feature list is that version's own `features.mk` (a switch you set keeps its setting in
   every version that has it). The page shows the same `git checkout` and `make` commands for a PC build.
5. **Build**. The first time it downloads the compiler (about 21 MB, kept by the browser); a build takes a few
   seconds. Then the page combines it with your files and shows every check (the same as
   `tools/ufwcheck.py` and `tools/uv5r_flasher.py`). You can download `firmware.bin` or the `.ufw`.
6. **Flash**: tick that you've read the risks, and (our update mode is not proven on a radio) that you can open
   the case and recover the radio with the ROM rescue mode; press Flash, pick the cable's port, then switch the radio on with **PTT-A** + `=` held. If an update
   stops part-way: switch the radio off and on with no keys and press Flash again; the loader carries on.

## What the site holds, and what it doesn't

| On the site | Licence |
| --- | --- |
| The page (`web/`), and the firmware sources (Makefile, `*.mk`, `src/`) of every tag, every branch's newest commit and the 40 newest commits that changed the build (`versions/`) | ours |
| `llvm.wasm.gz`: pi32v2-llvm's clang, ld.lld and llvm-objcopy as one WebAssembly program, the compiler's C headers and compiler-rt's builtins | Apache-2.0 with LLVM Exceptions |
| JieLi's `cpu.a`, `media.a`, `btctrler.a` with their `LICENSE` | Apache-2.0 ([THIRD-PARTY.md](THIRD-PARTY.md)) |

Never on the site: the stock `.ufw`, MM9PDY's patch, or anything made from them. Each visitor's browser fetches
those from the links the README already gives (`web/lib/fetchstock.js`) and builds the flashable `.ufw` itself, the
way `make ufw` does.

The page keeps the rules of the Python tools: the stock file must be exactly stock-nrf-v0.27; the app must carry
an update mode (the `mkufw.py` guard); before flashing, the container, CRCs, pinned loader, flash area, stock bytes
outside the app window and the update mode are checked (`check_image`), and the radio is only sent bytes of that
image that it asked for. Transmit is limited by the firmware itself (TX LOCK = HAM by default).

## How it works

| Part | File |
| --- | --- |
| Versions: the site's list, or any other commit fetched from GitHub (API for the file list, raw.githubusercontent.com for the files) | `web/lib/versions.js` |
| `features.mk` → the switches and their help | `web/lib/features.js` (as `parse_features` in `mksite.py`) |
| The version's own Makefile → the commands `make -n` would print; the page runs the clang, ld.lld and llvm-objcopy ones | `web/lib/make.js` (a GNU make reader: variables, conditionals, includes, pattern rules, functions), `web/lib/plan.js` |
| WASI runtime with an in-memory file system | `web/lib/wasi.js` |
| One compile or the link; a pool of Web Workers | `web/lib/buildcore.js`, `build-worker.js`, `build.js` |
| Fetching the stock file (MEGA's API, AES-CTR with the link's key) and the patch (raw.githubusercontent.com), both SHA-256 pinned | `web/lib/fetchstock.js` |
| `.ufw` tools: patch, mkufw with its guard, ufwcheck, image checks | `web/lib/ufw.js` (port of `tools/ufwpatch.py`, `mkufw.py`, `ufwcheck.py`, `uv5r_flasher.py`) |
| The cable update session over Web Serial | `web/lib/flasher.js` (port of `flash_radio` in `tools/uv5r_flasher.py`) |
| The toolchain as WebAssembly | `web/toolchain/build-wasm.sh`: pi32v2-llvm cross-built with wasi-sdk, with YoWASP's WASI patch and our small fix-ups (`*-fixups.pl`); about an hour on 24 cores |
| The site | `python web/tools/mksite.py` → `build/site/` |
| Publishing | `.github/workflows/pages.yml`: on every push to `main`, every tag, or by hand; needs Settings → Pages → Source = GitHub Actions once. The first run builds `llvm.wasm` (about two hours), later runs take it from the cache |
| Releases | `.github/workflows/release.yml`: push a tag `v*`; makes a draft release with `firmware.bin` for four switch sets and SHA-256 sums (no `.ufw`) |

## Make and check it on the PC

```sh
sh web/toolchain/build-wasm.sh ~/wasm                      # WSL or Linux, once per LLVM version
python web/tools/mksite.py --toolchain-wasm <~/wasm/out> --llvm-release <unpacked pi32v2-llvm linux release>
python -m http.server 8770 -d build/site                   # then open http://127.0.0.1:8770
```

| Test | Checks |
| --- | --- |
| `python tests/web_make_host.py` | at every commit that changed the Makefile or `features.mk`: make.js's commands equal `make -n -B`'s for 4 switch sets, and features.js reads `features.mk` as `mksite.py` does |
| `python tests/web_build_host.py --toolchain build/llvm-release/bin --wsl [--commit SHA ...]` | `firmware.bin` built with `llvm.wasm` (in Node) is byte for byte the native build of that commit (a `git archive`), 3 switch sets |
| `python tests/web_ufw_host.py` | the JS `.ufw` equals `tools/mkufw.py`'s byte for byte, passes `tools/ufwcheck.py`; NRFB stock and an app without update mode are refused |
| `python tests/web_flasher_host.py` | the JS flasher against the emulated radio of `tools/uv5r_flasher.py`: a whole update, and a power cut at 50 % then resumed |

Tested on the PC (2026-11-27): all four pass (web_make_host: 45 commits, 117 runs, 0 differ; web_build_host at
32650b6, 513abed and cc30834: identical). In Edge's engine (the Claude app's browser pane) the page built the
default and a no-FM/no-TX switch set in about 3 s each, byte for byte the native builds, and combined and checked
the `.ufw`; it switched between versions with 25, 18 and no switches, and fetched and built 7da3979 from GitHub. **Not tested: flashing a radio from the page** (Web Serial to a real K1 cable).

## Limits

- NRF only, like `make ufw`.
- The toolchain version is the one `llvm.wasm` was built from (`LLVM_TAG` in `build-wasm.sh`, now
  pi32v2-23.1.6-2); the native release you build with may differ.
- Every version is built with the site's toolchain and JieLi libraries, not the ones that commit was made with.
- The page reads the Makefile itself and runs no shell: `$(shell ...)` is empty (FW_BUILD is set on the command
  line), and recipes other than clang, ld.lld and llvm-objcopy are skipped. A Makefile that needs more (a generator
  step, say) makes `tests/web_make_host.py` fail.
- A commit fetched from GitHub uses two API requests; GitHub allows 60 an hour for each visitor without a login.
