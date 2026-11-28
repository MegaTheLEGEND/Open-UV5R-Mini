# Bluetooth

The UV-5R Mini's Bluetooth (used by the stock firmware for **Wireless CPS**) runs on the AC695N's built-in
Bluetooth radio. JieLi only ships its Bluetooth controller and stack as closed, precompiled libraries
(`btctrler.a`, `btstack.a`), so what a build has depends on which of them it links:

| Build | Bluetooth | Contains closed code | How |
| --- | --- | --- | --- |
| **BLE** (the normal build when JieLi's libraries are in `build/sdk/`) | BLE: scanner, beacon, a serial link to a PC or phone, CHIRP over Bluetooth | yes: JieLi's controller library `btctrler.a` (Apache-2.0, from their public SDK) | `make` in this repo with `ENABLE_BT_LE` (features.mk), bare metal; [below](#the-ble-backend-built-2026-11-27) |
| **Open** (no JieLi libraries) | no | no | `make` without `build/sdk/` (`src/bt/bt_none.c`); the Bluetooth apps say "none" |
| **SDK edition** (not finished) | classic SPP serial port | yes, JieLi's libraries | the radio app built as a task inside JieLi's AC695N SDK |

**On the radio:** MENU → BLUETOOTH has BLE SCAN, BT BEACON, BT SERIAL, BT CHIRP and BT INFO. Bluetooth is on only
while one of them is open. BLE SCAN, BT BEACON and BT INFO work on Sean's radio (2026-11-27); BT SERIAL and BT CHIRP
over BLE are written and emulator-tested, not yet tried on a radio.

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

This repository never contains JieLi code or libraries: `tools/sdk/fm_libs.sh` copies them from your clone of the
public SDK into `build/sdk/` (never committed). Their licence (Apache-2.0) allows images built with them to be
shared, with the licence kept ([THIRD-PARTY.md](THIRD-PARTY.md)).

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
| BLE backend (`src/platform/jl_sdk/bt_le.c`, `bt_glue.c`, `ENABLE_BT_LE`) | **scanning and advertising work on Sean's radio** (2026-11-27); one connection for the serial link: emulator-tested |
| GATT server with the Nordic UART Service (`src/bt/gatt.c`) | done, PC-tested (`tests/btle_host.py`) |
| Pairing: LE legacy pairing, passkey on the radio, keys made again instead of stored (`src/bt/smp.c`) | done, PC-tested (the spec's own c1/s1 examples, AES FIPS-197, whole pairings); not tried with a phone |
| Apps BLE SCAN, BT BEACON, BT INFO | work on Sean's radio |
| Apps BT SERIAL, BT CHIRP; PC bridge `tools/ble_serial.py` | written; not tried on a radio |
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

## Bluetooth API and Flipper-style apps (2026-11-27)

Sean asked for (1) the Bluetooth functions behind an API the apps can call, (2) whether the AC695N's Bluetooth radio
can act like an SDR, and (3) as many of Flipper Zero's Bluetooth features as the hardware allows.

### The API: `src/bt/bt.h`

One header for every backend. `BT_Caps()` says what the linked backend can do, so an app shows what is missing
instead of failing. All calls are from the main loop; received data and scan reports are polled.

| Part | Calls | Backend that has it |
| --- | --- | --- |
| What is there | `BT_Caps`, `BT_Backend`, `BT_GetAddress` | all |
| Serial link | `BT_IsAvailable`, `BT_Init`, `BT_IsConnected`, `BT_Send` | BLE backend (Nordic UART Service, `bt_le.c` + `src/bt/gatt.c`); SDK edition (SPP, `bt_spp.c`) |
| BLE scanning | `BT_LeScanStart`, `BT_LeScanStop`, `BT_LePoll` (`BT_LeReport_t`: address, RSSI, 31 bytes of data) | BLE backend |
| BLE advertising (the radio's own beacon) | `BT_LeAdvStart`, `BT_LeAdvStop` | BLE backend |

`src/bt/le_adv.c` reads advertising data (name, flags, TX power, service UUID, company ID, a guess at the kind of
device: iBeacon, Eddystone, Apple, Microsoft, Google, Samsung, Tile, HID, heart rate) and keeps the list of devices
heard (16, strongest first, aged out after 30 s). Pure C, tested on the PC: `python tests/btle_host.py`.

### Can the Bluetooth radio be an SDR? No.

- **No raw IQ.** The radio is run by JieLi's closed baseband and controller (`btctrler.a`). What comes out is
  packets, plus an RSSI for each packet. No register or library call hands out samples.
- **Only 2.4 GHz.** The synthesiser code (`bta_pll_*`) tunes the 2.402–2.480 GHz Bluetooth channels, and the antenna
  is a PCB trace sized for 2.4 GHz. Nothing known tunes it anywhere else.
- **Only Bluetooth modulations:** GFSK (BLE, BR), π/4-DQPSK and 8DPSK (EDR). The controller has test modes (LE
  receiver and transmitter tests on one of the 40 channels, a single carrier) and a per-channel RSSI. At most that
  gives a coarse 2.4 GHz activity meter: not a spectrum analyser, and it decodes nothing but Bluetooth.
- The FM receiver in the same chip (64–108 MHz, RE-NOTES) hands out demodulated audio, not IQ, so it is not an SDR
  either.

### Flipper Zero's Bluetooth features on this radio

| Feature | Possible? | Why / what it needs | State |
| --- | --- | --- | --- |
| BT info: backend, links, address, the stock BTIF bytes | yes | nothing more | **app `BT INFO`** (`ENABLE_APP_BTINFO`) |
| BLE scanner: devices nearby, name, RSSI, kind, raw data | yes | LE scanning on the controller (the BLE backend below) | **app `BLE SCAN`** (`ENABLE_APP_BTSCAN`); **works on Sean's radio** (2026-11-27). Debug builds also take made-up reports on the cable (`!app adv`) |
| The radio's own beacon (its name, an iBeacon or an Eddystone URL) | yes | LE advertising (the BLE backend) | **app `BT BEACON`** (`ENABLE_APP_BTBEACON`); **works on Sean's radio** (2026-11-27) |
| Serial link to a PC or phone | yes | BLE: our own small GATT server with the Nordic UART Service (`src/bt/gatt.c`). Classic SPP: the SDK edition | **app `BT SERIAL`** (`ENABLE_APP_BTSERIAL`): a terminal, MENU types a line; emulator-tested, not on a radio |
| CHIRP programming over Bluetooth (stock's Wireless CPS, done our way) | yes | the serial link, and a PC bridge (`tools/ble_serial.py`) | **app `BT CHIRP`** (`ENABLE_APP_BTCHIRP`); CHIRP side PC-tested, link not on a radio |
| BLE remote / keyboard for your own phone (media keys, camera shutter) | yes, later | HID over GATT: a host stack (`btstack.a` has it; the SDK's `hid` app) | todo |
| Classic device discovery (inquiry) | probably | the controller has inquiry; needs the classic HCI commands | todo |
| Connection sniffer (following other devices' links) | no | the controller reports only advertisements and our own links: no promiscuous mode, no raw packets | — |
| "BLE spam" (fake pairing pop-ups on nearby phones) | not built | it floods other people's devices; not something this project ships | — |

### The BLE backend: what it takes (findings, 2026-11-27)

The plan: JieLi's controller library (`btctrler.a`, from the public SDK, Apache-2.0) linked into our bare-metal
build as the FM receiver is, with HCI commands sent to it directly. The scanner and the beacon need no host stack.

- **The controller speaks standard HCI.** `btctrler.a` defines `hci_controller_init`, `hci_le_controller_cmd_handler`,
  `hci_send_cmd`, `hci_send_event` and an H4 transport (`h4_uart_init`, `h4_hci_push_data`), and the task entry
  `btctrler_task_init(transport, config)` (SDK `include_lib/btctrler/btctrler_task.h`). So the open question of
  "A smaller option" above is answered: yes. NimBLE could sit on top later for GATT.
- **Size.** A test link of `btctrler_task_init` and the HCI handlers pulled in about 290 KB of code and 11 KB of
  RAM (`.bd_base` 4 KB, HCI 2.7 KB, RF 1 KB, BLE link layer 0.8 KB, …). Classic code comes along because the library
  picks its modes from `config_*` variables at run time. Our image is about 117 KB of a 479 KB slot, so the code
  fits. The RAM does not fit in `.bss` (full): the backend would run only while a Bluetooth app is open, with its
  memory in the FM radio's 24 KB heap (`.ram2`, free outside the FM app).
- **What it expects of JieLi's OS: 230 symbols**, in these groups (our stand-ins would sit beside `fm_glue.c`):

  | Group | Examples | Our stand-in |
  | --- | --- | --- |
  | settings it reads | `config_btctler_mode`, `config_btctler_le_*`, `CONFIG_*` | constants: BLE only, observer and broadcaster |
  | tasks and queues | `task_create`, `os_taskq_pend`, `os_taskq_post_type`, `os_sem_*`, `os_mutex_*`, `os_time_dly` | the hard part: the controller's task waits in `os_taskq_pend`, so it needs its own stack and a small context switch (pi32v2 assembly) run from the main loop |
  | timers | `sys_timer_*`, `sys_timeout_*`, `usr_timeout_*` | a timer list run from the 10 ms tick |
  | interrupts | `request_irq`, `local_irq_*`, `bit_set_ie` | `driver/irq.c`, as FM |
  | memory | `malloc`, `zalloc`, `free`, `lbuf_*` (`system.a`) | the FM heap; `lbuf` from `system.a` or our own |
  | power and clocks | `low_power_*`, `clk_get_osc_cap`, `p33_soft_reset`, `adc_*` | no sleep; crystal trim from the stock values |
  | storage | `syscfg_read`, `syscfg_write` (address, name, RF trim) | read only from BTIF and calibration; never written (AGENTS.md rule 6) |
  | pairing crypto | `uECC_*`, `bi_*`, `f1_function`, `g_function` | not needed to scan or advertise: stubs that refuse |
  | logs | `log_tag_const_*`, `printf`, `puts` | quiet |

- **Clocks and RF set-up** are inside the library (`bt_rf_init`, `bt_analog_part_init` writing WLA_CON
  `0x1E1900`..; RE-NOTES "Radio clocks"); stock's start-up order (`sub_01E0173E`) is the reference. The same set-up
  may help the weak FM radio too (FM shares the BT_RF pin).

### The BLE backend, built (2026-11-27)

`ENABLE_BT_LE` (features.mk, on when `build/sdk/btctrler.a` is there and the FM glue is built):
`src/platform/jl_sdk/bt_le.c` (bt/bt.h on the controller) and `bt_glue.c` (the stand-ins). What reading the library
showed, and what we did:

- **No OS needed.** The SDK's controller task (`btctrler_task`, read from the library's LLVM bitcode with
  `clang -S -emit-llvm`) is `hci_controller_init()`, then a loop: take a message (`os_taskq_pend`), hand it to
  `ll_hci_cmd_handler` / `ll_event_handler`, run `hci_controller_run_loop()`. The controller's own work runs in
  cooperative "threads" inside the library. So `BTLE_Tick` (called from `APP_Tick`) runs that loop body from our
  main loop, and `os_taskq_post_type` is a 16-message queue (`bt_glue.c`). No second stack, no context switch.
- **JieLi's own host interface, as their host uses it** (`config_btctler_hci_standard = 0`): `ll_hci_scan_set_params`,
  `ll_hci_scan_enable`, `ll_hci_adv_set_params`, `ll_hci_adv_set_data`, `ll_hci_adv_enable`. Events come back as
  standard HCI event packets to `hci_packet_handler` (the in-chip transport `hci_transport_h4_controller_instance`),
  where LE Advertising Reports (0x3E/0x02) become `BT_LePoll` reports.
- **Settings are ours** (`config_*` in `bt_glue.c`): BLE only; scanning, advertising and one connection as a
  peripheral (for the serial link); normal mode. `bt_glue.c` is compiled as LLVM bitcode (`-flto`), so link-time
  optimisation sees these values with the library and drops classic Bluetooth, TWS and the test modes: **Bluetooth
  adds about 40 KB of code** (the image is 160 KB of 479 KB) instead of about 285 KB, and 3.8 KB of RAM.
- **RAM:** the controller's variables are in low RAM 0x3000..0xC000 (`btram`, firmware.ld; stock's RAM code ran
  there, ours does not), cleared and filled at every Bluetooth start; the rest of that region is the Bluetooth heap
  (`malloc` takes from it while Bluetooth runs, `free` goes by address, so the key beeps' DAC keeps the FM heap).
  To make room in `.bss` for the library's few plain variables, the key test's flash check now borrows the update
  loader's buffer (`UPDATE_Scratch`) instead of 8 KB of its own.
- **Interrupts:** the controller asks for 38 (receive) and 39 (events), priority 2; they are real interrupt
  functions in the library. The Bluetooth start switches interrupts on; FM's open and close (key beeps) leave the
  table alone while Bluetooth runs.
- **Kept out:** two symbols would have pulled JieLi's start-up code and its settings-flash driver (`vm.c`, which
  erases the VM area) out of `cpu.a`; `bt_glue.c` defines harmless stand-ins (`__BT_UPDATA_JUMP`,
  `spi_cache_way_switch`). Storage calls (`syscfg_*`) read nothing and write nothing (AGENTS.md rule 6). The address
  is a random static one made at start, not the stock one.
- **Clocks and power** as stock's Bluetooth start-up: CLK_CON1 radio clocks, VDC13 1.40 V, `bt_pll_para(24 MHz,
  48 MHz)`, `bt_max_pwr_set(…, BLE 7)` (+2.1 dBm).
- **Starts only on demand:** the first `BT_LeScanStart`, `BT_LeAdvStart` or `BT_Init` (BLE SCAN, BT BEACON,
  BT SERIAL, BT CHIRP) starts it, and leaving the app stops it. BT INFO shows CONTROL: OFF, STEP n (where a start stopped) or ON with the events seen.
  Debug builds: `!app bt` in BLE SCAN prints the same.
- **Tested in the emulator** (`python emu/bt_ours.py --isa ../pi32v2-isa --quiet --adv --serial`): the start runs to the
  end (about 190 000 instructions, with JieLi's calibration delays skipped and the SAR ADC's done bit faked), the
  scan and advertising commands reach the controller (1 and 5 messages), stopping works. The analog block
  (WLA_CON 0x1E1900..) gets the same 16 registers as stock's Bluetooth start-up (build/bt_init.txt); 9 end on
  the same value, the others on calibration results, which come from the real chip. The emulator has no radio,
  so no packets. **Not run on a radio.**

### BT SERIAL and BT CHIRP: the BLE serial link (2026-11-27)

The serial link needs a GATT server, and JieLi's is in `btstack.a` (a whole host stack). We wrote a small one
instead: **`src/bt/gatt.c`**, pure C, about 300 lines, from the Bluetooth Core Specification. It serves two services:

| Service | Characteristics |
| --- | --- |
| GAP (0x1800) | device name `UV5R-MINI`, appearance |
| Nordic UART Service `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | RX `6E400002-…` (the phone writes), TX `6E400003-…` (the radio notifies; switched on by its CCCD) |

The Nordic UART Service is what most "BLE serial" phone apps (Serial Bluetooth Terminal, nRF Connect, Bluefruit
Connect) and Web Bluetooth pages speak. ATT MTU stays 23, so a notification carries 20 bytes and every packet fits
one 27-byte link-layer packet. Pairing is our own too ([below](#pairing-mode-2026-11-27)); nothing is written to
flash. `python tests/btle_host.py` walks a phone's discovery, writes and notifications through it (73 checks with
the advertising parser).

How it runs (`bt_le.c`): `BT_Init(handler)` starts Bluetooth and advertises connectable (`ADV_IND`, every 100 ms)
with the name, and the service UUID in the scan response. A phone or PC connects (LE Connection Complete); its ACL
packets arrive at `hci_packet_handler`, are put together into L2CAP frames, queued, and answered by the GATT server
from the main loop (`BTLE_Tick`). Answers and notifications go back with the library's `le_hci_send_acl_packet`,
at most 6 in flight (credits come back with Number Of Completed Packets). `BT_Send` splits data into 20-byte
notifications. When the link drops, advertising starts again. `BT_Init(0)` disconnects and stops.

**BT SERIAL** (MENU → BLUETOOTH): shows what comes in (9 lines), RX/TX byte counts, LINKED or WAITING. MENU types a
line with the keypad (the [typing service](RADIO.md#typing-text)) and MENU again sends it with CR LF; 1 sends
`PING`, 2 the frequency, 0 clears.

**BT CHIRP** (MENU → BLUETOOTH): the cable's CHIRP protocol (`src/app/program.c`) on the BLE link
(`PROGRAM_BT`, `PROGRAM_PollBt`). It stays open for several downloads and uploads; after an upload the radio loads
the new channels and settings at once. CHIRP can't open a BLE device itself, so a small bridge on the PC turns the
link into a TCP port:

1. On the PC, once: `pip install bleak`.
2. On the radio: MENU → BLUETOOTH → BT CHIRP. It shows WAITING.
3. On the PC: `python tools/ble_serial.py`. It finds `UV5R-MINI`, connects, and says `listening on 127.0.0.1:5005`.
   The radio shows LINKED.
4. In CHIRP: Radio → Download from radio. Port: **Custom…**, type `socket://localhost:5005`. Vendor Baofeng, model
   UV-5R Mini.
5. Upload the same way. Leave BT CHIRP with EXIT when done.

If the build only lets paired PCs in (`ENABLE_BT_PAIRED_ONLY=1`), pair once first: press 5 on the radio (pairing
mode), then Windows Settings → Bluetooth → Add a device → UV5R-MINI, and type the six digits the radio shows.

`python tools/ble_serial.py --scan` lists radios in range; `-v` prints the bytes both ways. The same bridge works for
BT SERIAL with any TCP terminal (`ncat localhost 5005`).

### Pairing mode (2026-11-27)

`src/bt/smp.c`, our own Security Manager (Bluetooth Core Vol 3 Part H), the radio as the responder:

| Part | How |
| --- | --- |
| Pairing mode | **5** in BT SERIAL or BT CHIRP: on for 60 s (the line shows `PAIRING ON 59S`), 5 again for off; it ends by itself once a phone has paired. Outside it a pairing request gets "pairing not supported" |
| Method | LE legacy pairing. The radio is DisplayOnly. A phone or PC that can type and asks for protection (Windows, Android, iOS do) gets **passkey entry**: the radio shows `KEY 123456`, typed on the phone or PC. Others get Just Works |
| Bonding with no key store | The long-term key we hand out is AES(ER, EDIV‖Rand), and the phone keeps EDIV and Rand with it. When it comes back it sends them, and the radio makes the same key again. ER is a secret made at each Bluetooth start from this radio's own BTIF block (0xFE000) and calibration bytes, never written, so a paired phone reconnects encrypted after power cycles with nothing stored (AGENTS.md rule 6) |
| Encryption | the controller's (LE Encryption feature on: `config_btctler_le_features = 1`); the key comes from `SMP_LongTermKey` on each LE Long Term Key Request |
| Paired only (optional) | `ENABLE_BT_PAIRED_ONLY=1`: the serial service answers "insufficient authentication" until the link is encrypted, so only a paired phone or PC can use BT SERIAL or program the radio with BT CHIRP. Off by default |
| Line on screen | `5 PAIRING MODE`, `PAIRING ON nnS`, `KEY nnnnnn`, `PAIRED, ENCRYPTED`, `PAIR FAILED` |

Limits: no LE Secure Connections (it needs P-256 ECDH, not written), so the pairing is the old kind: someone who
records the pairing itself over the air can work the key out (more work with a passkey). Phones that insist on
Secure Connections only won't pair. Forgetting a phone: remove the radio on the phone; the radio keeps nothing. A
radio's ER changes only if its BTIF or calibration bytes change. Random numbers come from AES under ER of a counter,
the time and the radio's own Bluetooth events. `python tests/btle_host.py` runs whole pairings (passkey, wrong
passkey, Just Works, a reconnect with the kept key) against the module.

**Not yet:** none of this has run on a radio. The CHIRP protocol is tested on the PC (`tests/chirp_host.py`) and the
link's start in the emulator, but no phone or PC has connected yet. The BLE remote (HID over GATT) is the next one the
same GATT server could carry; classic device discovery stays off (classic is switched off to save 260 KB).
