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

**Update 2026-11-25: Wireless CPS code, and our radio in the SDK build.** `program.c` takes CHIRP over SPP as
well as the cable, and `build_sdk.sh --radio` links our radio code as a task of the SDK app (sections below).
Still no `.ufw`: what is missing is in "From `sdk.elf` to a flashable `.ufw`".

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

You need JieLi's public BR23 SDK ([fw-AC63_BT_SDK](https://github.com/Jieli-Tech/fw-AC63_BT_SDK)) cloned with Unix
line endings, the open pi32v2 LLVM toolchain and the **drop-in** from
[jvitkauskas/pi32v2-isa](https://github.com/jvitkauskas/pi32v2-isa) (`docs/DROPIN.md`); the recipe is in
[tools/sdk/README.md](../tools/sdk/README.md). Linux or WSL. Then:

```sh
sh tools/sdk/build_sdk.sh --radio ~/pi32v2/sdk ~/pi32v2/jieli      # sdk.elf with our radio code in it
```

`--radio` runs `tools/sdk/add_radio.sh`, which changes the SDK clone in place (nothing from it comes back here):

1. copies `src/`, `features.mk`, `sources.mk` and `tools/sdk/open_uv5r.mk` to `SDK/open_uv5r/`;
2. the app's BR23 board Makefile includes `open_uv5r.mk`: every file of `sources.mk` except `src/main.c`,
   `src/start.S` and `src/bt/bt_none.c`, plus `src/platform/jl_sdk/`, built with our feature switches and
   `PROGRAM_BT=1`. The update mode (`src/update/`) is in, as in every app;
3. the board configuration: classic Bluetooth (EDR, so SPP) on; the board's code switch, IO keys and ADC keys off
   (they would drive PB1, PB6, PB7, which our drivers own); low-power sleep off. PC/USB and the charger are already
   off in that board;
4. `open_uv5r_start()` (our radio task) is called right after `btstack_init()` in the app's start;
5. the SDK's own SPP module (a loopback demo) calls `open_uv5r_spp_attach()` once the stack is up, so SPP data
   comes to us.

## Wireless CPS in the Bluetooth edition (2026-11-25)

Stock's Wireless CPS is the cable programming protocol sent over SPP: one command handler, the received bytes put
in the cable's command buffer, and one reply function that sends to SPP or the UART by a "link is Bluetooth" flag
(`sub_01E4CA10`). The stock findings are written up on branch `re/peripheral-sweep` (commit `c0869d1`, section
"Stock's Wireless CPS, read from its code"). Ours does the same:

| Part | How |
| --- | --- |
| `src/app/program.c` | built with `PROGRAM_BT=1`: `PROGRAM_BtReceive` queues bytes from Bluetooth (1 KB, any task); `PROGRAM_Poll` looks for `PROGRAMCOLORPROU` on each link on its own, runs the session on the link it came from and sends every reply back over it. Screen: `CHIRP BT`. The open build (`PROGRAM_BT=0`) reads only the cable |
| `src/platform/jl_sdk/bt_spp.c` | registers our receive and state callbacks with the library's SPP; sends with the public SDK's `user_send_cmd_prepare(USER_CTRL_SPP_SEND_DATA, len, data)` (up to 512 bytes; retried for 0.5 s while it returns 3, "previous packet still going out"); `BT_WaitMs` is the OS delay, so the stack runs while `program.c` waits |
| `src/platform/jl_sdk/radio_task.c` | our `APP_Init`/`APP_Tick` loop as an OS task at priority 1, below the Bluetooth tasks (btctrler 4, btstack 3), since parts of our code still wait by spinning |
| SPP service | the library's own: SDP name `JL_SPP` (as stock), **RFCOMM channel 1** (stock's record says 2). The channel is fixed inside `btstack.a` (`spp_profile.c.o`; the record is `sdp_spp_service_data` in `sdp_util.c.o`); a PC or phone finds it through SDP, so it is left as it is |

Tested on the PC: `tests/chirp_host.py` runs CHIRP's own UV-5R Mini driver against `program.c` over the cable (open
build), over the Bluetooth queue (nothing may go out on the cable) and over the cable in the Bluetooth build.
Not tried with a real Bluetooth link, CHIRP on a Bluetooth COM port, or stock CPS. A session over Bluetooth waits
in 10 ms OS ticks, so a download may take a few seconds longer than over the cable.

Known small gap: the key test (`keytest.c`) calls `BT_Init` with its own loopback handler, so after it has run,
Bluetooth bytes go to it, not to `program.c`, until the next power-on.

The SDK build (2026-11-25): `ac635n_spp_and_le` with EDR on and our radio code **links** (`sdk.elf`): code
272 KB, initialised data 65 KB in flash; `app.bin` (pi32v2-isa's `postbuild.sh`) 332 KB, inside stock's app slot
(0x77AC8 = 479 KB). Our code survives link-time optimisation (`APP_Tick`, `UPDATE_Mode`, `BT_Send`,
`PROGRAM_BtReceive`, the `PROGRAMCOLORPROU` string). Never run, in the emulator or on a radio.

## From `sdk.elf` to a flashable `.ufw`: what is missing (2026-11-25)

Worked out, not built. The route that keeps the stock update mode is the one the open build uses: `tools/mkufw.py`
puts the app into the app slot of stock's NRF `.ufw` and keeps stock's bootloader (`uboot.boot`), `isd_config.ini`,
app area directory (`PRCT`, `VM`, `BTIF`), `key_mac` and the loader in `ota.bin` byte for byte. JieLi's own
packaging (`cpu/br23/tools/download.sh`: `host-client`, `strip-ini`, `lz4_packet`) is closed and makes its own
flash layout, so it is not an option. What stands between the two:

| # | Gap | Why it matters | What would settle it |
| --- | --- | --- | --- |
| 1 | **The start-up copies `.data` from the wrong place.** `sdk.ld` sets `data_begin = text_begin + SIZEOF(.text)` = `0x01E2252C`, the end of the *first* `.text`. The SDK's `.ld` fragments (`update.ld`, `driver_lib.ld`, …) each add a `.text` output section; ld.lld keeps them as separate sections (a second, 104 KB `.text` from `0x01E22530`, two more after it, and separate `.data`/`.bss` at RAM `0x13798`), where JieLi's linker apparently merges them. So the start-up would copy code as `.data`, and the extra `.data`/`.bss` are never set up. The SDK demo build has the same split | an image built this way crashes at start-up, before the update-mode check: a radio running it could not be flashed by cable | merge the fragments into one `.text`/`.data`/`.bss` for ld.lld (an SDK fix in `build_sdk.sh`), then check that `data_begin` is the end of all code and that `postbuild.sh`'s `app.bin` matches what `download.sh` makes (`text.bin` + `data.bin` + overlays) |
| 2 | **Link address.** The SDK links code at `0x01E00120` (`code0` in `sdk_ld.c`, `CONFIG_ENTRY_ADDRESS` in `isd_config_rule.c`: JieLi's packer puts a 0x120-byte directory first). Stock's slot, which `mkufw.py` fills, runs at `0x01E00100` | 32 bytes off: nothing would run | link at `0x01E00100` (one more SDK fix), and check the entry address in stock's `isd_config.ini` |
| 3 | **Our update mode inside JieLi's OS.** `mkufw.py` accepts this `app.bin` today: it finds our update-mode marker (`uv5r_flasher.update_mode_status` says "ours"). But that code has only run bare metal. Here it runs in the radio task, after the OS, interrupts, Bluetooth and the SDK's flash driver are up, and it writes the loader slot with our flash driver while other tasks and interrupts may run code from flash. The hand-over record at RAM `0x2FF88` is inside the area the SDK reserves for it too (`UPDATA_BEG` `0x2FF80`), which is good | the guard passing proves nothing for this build | run `UPDATE_Requested`/`UPDATE_Mode` before the OS starts (early in the SDK's `app_main`), or with interrupts and the other tasks stopped; test it in the emulator. Until then the SDK build should not carry the marker, so that `mkufw.py` refuses it |
| 4 | **The SDK's VM code and stock's `VM` area.** The SDK keeps its settings with its own VM code (`CONFIG_ITEM_FORMAT_VM`, `VM_MAX_SIZE_CONFIG` 16 KB); stock's `VM` is `0x94000`-`0xFE000`, written by stock's older SDK. If `system.a`'s VM start-up does not recognise what it finds, it may format the area | AGENTS.md rule 6: `VM` must never be erased by our code | trace the VM start-up in `system.a` (bitcode: compile it to an object, as done here for the SDP record) or run it in the emulator against a VM dump; if needed, point the SDK's VM at a free area or turn it off |
| 5 | **Bootloader to app.** Stock's `uboot.boot` was built with stock's SDK; the app reads what it leaves behind (`.boot_info`; flash and clock settings from `isd_config.ini`). The public SDK is a newer release (`SDK_VERSION_CFG_DEFINE 0x240001`, 2024) | an unknown mismatch stops the app early | compare the stock app's start-up with the SDK's (`cpu.a`) in the lifted C; run it in the emulator |
| 6 | **Files the SDK opens in flash.** It finds files in the app area directory by name (`JL_BT_CFG_FILE_NAME`, `JL_RESERVED_VM_FILE_NAME`, `JL_OTA_LOADER_FILE_NAME`, …) for the Bluetooth name, MAC address and its own update support. Stock's directory has `PRCT`, `VM`, `BTIF`, `key_mac`, `tone` | a missing file may stop Bluetooth or the app | list what `syscfg` and `sdfile` open (lifted C or emulator) and what stock's directory offers |
| 7 | **No run anywhere** | | the emulator (`emu/`) runs the stock app; running this `app.bin` there up to the radio task and the update check would settle 1, 2, 5 and much of 3 and 6 before anyone flashes |

Nothing here is built around: `mkufw.py` is unchanged and no `.ufw` was made. Rule 3 stands: none of this is safe
to flash.

## Status

| Part | State |
| --- | --- |
| `src/bt/bt.h` interface, open stubs | done (`BT_WaitMs` added 2026-11-25) |
| SPP glue (`src/platform/jl_sdk/bt_spp.c`) | Wireless CPS: SPP data to `program.c`, replies with `USER_CTRL_SPP_SEND_DATA`; compiles and links in the SDK build (2026-11-25); not run |
| OS task glue (`src/platform/jl_sdk/radio_task.c`) | compiles and links; priority 1; not run |
| Key-test app | shows the Bluetooth state and echoes received bytes back (loopback test) |
| Wireless CPS protocol | the cable protocol over SPP (stock code read 2026-11-26); `program.c` speaks it on both links, tested on the PC with CHIRP's driver (above) |
| BR23 SDK build with open tools | `tools/sdk/build_sdk.sh`: `ac635n_spp_and_le` links (`sdk.elf`, `app.bin`); with `--radio` also our radio code (2026-11-25); no `.ufw`, not run |
| SDK integration script | `tools/sdk/add_radio.sh` (or `build_sdk.sh --radio`), 2026-11-25 |
| `.ufw` package | not made; what is missing is listed above |

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
