# Road map to a working C firmware

What still has to be written before the C build can replace stock v0.27 on a radio. Status as of 2026-11-20.

Legend: **done** = written and tested (on the PC or emulator); **partial** = some of it written; **todo** = not started;
**unknown** = needs reverse-engineering first.

## Approach

- **Written from scratch.** The firmware is our own C. The stock firmware is the reference for how the hardware
  works: pins, timings, register values. The lifter and the emulator make it quick to read.
- **Borrow from the UV-K5's open firmware where it fits.** It drives the same radio chip (BK4819), so most of the
  radio and UI code can be ported, with the chip layer replaced. DualTachyon's firmware and egzumer's fork are
  Apache-2.0 (check each repo's LICENSE): keep their copyright headers and include their licence and NOTICE. This
  repo's own licence is still to be chosen; Apache-2.0 would match.
- **First image: a welcome screen and the update mode only**, with the update check as the first thing the app does.
  Before it goes on a radio, prove a recovery route ([TESTING.md](TESTING.md#if-the-app-itself-doesnt-start-not-verified)).
- **Transmit limited to the legal bands by default**, with wide receive, as UV-K5 builds usually do.
- **Both kinds of radio (NRF and NRFB):** the hardware is the same as far as the code shows, but the settings area
  sits in a different place, so builds need a setting for each ([RE-NOTES.md](RE-NOTES.md#nrf-v027-and-nrfb-v004)).

## 1. Before the first flash (safety)

A radio running an app without these cannot be flashed again without opening the case.

| # | Item | Status | Notes |
| --- | --- | --- | --- |
| 1.1 | Update-mode protocol (ping, rate, reads, container and loader checks, hand-over record) | **done** | `app/update.c`, tested by `tests/update_host.py` against the real flasher host |
| 1.2 | Loader store: choose the flash address, erase, write, read back and verify | **done** | `UPDATE_StoreLoader` writes the same bytes at the same addresses as the stock allocator (learnt in the emulator, `emu/loader_store.py`); checked by `tests/update_host.py` |
| 1.3 | Hand-over reset: record to 0x2FF88, close interrupts, reset into the bootloader | **done** (untested on hardware) | `port_handover` in `app/update_radio.c` plus `app/handover.S` (run from RAM), our own version of the stock steps at `0x020011E6`; never reached on a radio yet |
| 1.4 | Cable UART on the USB pins (RX 61 = D+, TX 62 = D−): pin routing and IO map | **partial** | `driver/uart.c`; on a radio (2026-11-21) pings over the K1 cable are answered at 9600 baud (48 MHz clock), but receiving stalls: the RX time-out flag doesn't come (UPDATE-MODE.md). Last build went silent: to find |
| 1.5 | Internal-flash driver: read, page program, sector/block erase, JEDEC id | **partial** | our own RAM code (`driver/flash.c`): ID, read, 4 KB erase, program and read-back work on a radio (2026-11-21). The update mode now uses it, and our own hand-over (`app/handover.S`); `tests/update_host.py` passes; not yet on a radio |
| 1.6 | Run the C update mode on the CPU emulator, then on a radio with a case-open recovery path at hand | **partial** | on a radio with the rescue route ready: ping works, the first flasher run stalled before anything was written; the radio is back on stock (full restore, byte for byte) |
| 1.7 | Setting (menu item, and a key combo at power-on) that restarts into the chip's ROM USB download mode, so a flash backup or recovery needs no key-sending dongle ([tools/jl_usbkey](../tools/jl_usbkey/README.md)) | **todo** | how the stock code or JieLi's SDK jumps to the ROM's download mode is to find; still needs the `G M P` wires, as the K-plug carries only D− |
| 1.8 | Case-open recovery route: ROM USB download mode through the `G M P` pads | **done** (one radio) | entered with an ESP32 ([tools/jl_usbkey](../tools/jl_usbkey/README.md)); full 1 MB read, and erase, write and read-back of a test sector, then the flash equal to the backup (RE-NOTES). Full restore of all 256 sectors from the backup: done, equal byte for byte |
| 1.9 | Flasher guard for the cable route: before sending any image, check that its app keeps a working cable update mode (entered with PTT A + `=`, answers ping, rate and reads, stores the loader, hands over) and never writes PRCT (calibration) or VM (settings) outside the loader slot. If it can't prove that, refuse, or show a big warning that the radio can then only be recovered by opening the case and using the ROM USB rescue mode (`G M P` wires and a key sender, 1.8) | **partial** | `tools/uv5r_flasher.py` and `tools/mkufw.py` already refuse images without the *stock* update mode; our own update mode needs its own check, and the warning text is to write |

## 2. Platform (chip and board)

| # | Item | Status | Notes |
| --- | --- | --- | --- |
| 2.1 | Start-up: clocks and PLL, cache, RAM code copy, `.bss` | **partial** | runs on a radio (2026-11-21): `start.S` keeps the boot record and puts the stacks at 0xD910 as stock; the CPU runs at about 0.75 delay-loop turns per µs (slow: clocks not set up yet) |
| 2.2 | Watchdog | **partial** | runs after the bootloader; cleared with `p33_or(0x80, 0x40)` through our own P33 access, which keeps our app running on a radio; timeout to find |
| 2.3 | Power: hold (PB10), off switch sense (PB11), power-off sequence | **partial** | pins known; PB11 high = off (seen on a radio); power-off in the key-test app, not yet tried on a radio |
| 2.4 | Timer tick and interrupts (vector table, `rti`), 10 ms time slice | **todo** | stock uses `APP_TimeSlice10ms` 0x01E4F1AA |
| 2.5 | ADC: battery voltage and level, low-battery warning and cut-off | **unknown** | channel and divider to find |
| 2.6 | LEDs (TX red / RX green), backlight dimming and timeout | **partial** | backlight PB7 known; LED pins unknown |
| 2.7 | Audio: speaker amplifier enable, routing BK4819 audio to the speaker, beeps and tones | **unknown** | amplifier pin still open (RE-NOTES) |
| 2.8 | External SPI flash driver (CS PA3 on SPI1) | **todo** | protocol known (`03` read); contents unknown: needs a dump from a radio |

## 3. Radio (BK4819)

| # | Item | Status | Notes |
| --- | --- | --- | --- |
| 3.1 | Register access, RX/TX switch, set frequency | **done** (basic) | `driver/bk4819.c` |
| 3.2 | Band and filter switching (VHF/UHF LNA and PA select pins), TX power levels | **unknown** | GPIOs and register values to find |
| 3.3 | Per-radio calibration (record sector at 0x93000): TX power, squelch, battery | **unknown** | read-only; must never be erased |
| 3.4 | Squelch (RSSI, noise, glitch), S-meter | **todo** | |
| 3.5 | CTCSS and DCS encode and decode, tone and code scan | **partial** | lookups found in stock (`CTCSS_FindNearest`, `DCS_GolayEncode`) |
| 3.6 | Bandwidth (wide/narrow), AM/FM demodulation, mic gain, VOX, DTMF | **todo** | |
| 3.7 | TX/RX state machine: PTT A/B, time-out timer, busy lock, roger beep, TX inhibit outside bands | **partial** | PTT handler located (`PTT_Handler`) |
| 3.8 | Dual watch, scanning (channel, frequency, priority) | **todo** | |
| 3.9 | FM broadcast radio | **unknown** | which chip receives it to find |

## 4. User interface

| # | Item | Status | Notes |
| --- | --- | --- | --- |
| 4.1 | LCD driver, rectangles, pixels, text | **done** | `driver/st7735s.c`, `ui/` |
| 4.2 | Fonts and icons as stock | **unknown** | may live in the external flash (0x0C0000 area) |
| 4.3 | Keypad scanning, long press, F-key layer, key lock | **partial** | matrix done (`driver/keyboard.c`); layers todo |
| 4.4 | Main screen: VFO/channel A and B, frequency, name, S-meter, battery, icons | **todo** | |
| 4.5 | Menu system with every stock menu item | **todo** | menu strings found in `app.bin` |
| 4.6 | VFO entry, channel store/delete, power-on screen and messages | **todo** | |

## 5. Data and compatibility

| # | Item | Status | Notes |
| --- | --- | --- | --- |
| 5.1 | Settings store compatible with stock (JieLi VM format, banks at 0x94000–0xFE000) | **unknown → partial** | VM module located (0x01E42380–0x01E42F00); format to document |
| 5.2 | Channel memory format as stock (so existing radios keep their channels) | **unknown** | likely in the external flash 0x000000–0x009000; Codeplug Studio documents the clone layout |
| 5.3 | Programming protocol in normal mode (clone read/write for CHIRP, Codeplug Studio) | **todo** | Codeplug Studio documents it |
| 5.4 | Bluetooth / Wireless CPS | **todo** | optional edition with JieLi blobs (docs/BLUETOOTH.md) |

## 6. Tooling and tests

Tools that work on stock-derived files produce them on your own PC, in `build/`, from your own copy of the stock
firmware: they are never in the repo.

| # | Item | Status | Notes |
| --- | --- | --- | --- |
| 6.1 | Matching build, flasher with emulated radio, image checks | **done** | |
| 6.2 | CPU emulator able to run the C build to its main screen | **partial** | boots stock part way; the C build needs no RTOS, so it is the easier target |
| 6.3 | External flash dump from a real radio | **done** | read whole (2 MB, ID `C46015`) over USB with [tools/jl_extflash](../tools/jl_extflash/README.md); layout in RE-NOTES (channels, settings, fonts, pictures; top 1 MB blank). Next: decode for 4.2 and 5.2 |
| 6.4 | Hardware confirmations: speaker amp, band switch, LEDs, ADC channel | **todo** | a radio and a multimeter, or more RE |
| 6.5 | Lifter: turn the stock app into C on your own PC, checked against the stock code | **done** | `python tools/lift/lift.py` with your own stock `.ufw` unpacked; output goes to your `build/lift/` ([LIFTER.md](LIFTER.md)) |
| 6.6 | Readable-C rewrite of the lifted functions with a local model (Ollama), each one tested | **done** (tool) | `python tools/lift/local_rewrite.py`; runs for hours on your PC, output in your `build/readable/` |
| 6.7 | NRFB radios (firmware V0.04): the same firmware rebuilt, app 596 bytes bigger, settings area 4 KB further on | **partial** | `tools/setup.py` unpacks it; `tools/disasm.py` and `tools/lift/lift.py --variant nrfb` work (1,974 code functions, 100% lifted); tests, emulator, builds and flasher patches are still NRF-only |
| 6.8 | Compare NRF and NRFB function by function | **done** | `tools/variant_diff.py` (needs both lifted) |

## Order of work

1. Section 1 (safety), then flash the C build once with a recovery path ready.
2. 2.1–2.4 and 5.1 so the radio boots, keeps time and keeps its settings.
3. 3.1–3.4, 3.7 and 2.7 for a radio that receives and transmits on one frequency.
4. 4.3–4.6 and 3.5 for everyday use; then 5.2–5.3 so programming software works.
5. The rest (scanning, dual watch, FM radio, DTMF, Bluetooth).
