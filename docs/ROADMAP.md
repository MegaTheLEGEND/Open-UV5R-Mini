# Road map to a working C firmware

What still has to be written before the C build can replace stock v0.27 on a radio. Status as of 2026-11-21.

## Now: top priority

1. **Cable update mode (1.4, 1.6): works on a radio at 9600** (2026-11-22, one radio): our firmware installed
   from stock and stock put back from ours, twice each way, with only the K1 cable
   (`tools/uv5r_flasher.py --loader-baud 9600`, about 10 minutes); a cut update resumes. Left: 38400 (19200 works once
   the rescue ESP32 is disconnected from the D+/D− wires), fewer retries, and the official Baofeng updater (not tried; its
   `.ufw` has no loader). Details at the end of [UPDATE-MODE.md](UPDATE-MODE.md).
2. **Bug: dual watch doesn't work on a radio** (3.8). Seen 2026-11-21 with the build at `0698323`: to find out what
   it does wrong (doesn't switch, doesn't stop on a signal, or the wrong side opens).
3. **CHIRP (5.3): written, PC-tested** (2026-11-22): CHIRP's own UV-5R Mini driver, unchanged, downloads and uploads
   against our programming mode (`src/app/program.c`, `python tests/chirp_host.py`). Not yet tried on a radio.

Legend: **done** = written and tested (on the PC or emulator); **partial** = some of it written; **todo** = not started;
**unknown** = needs reverse-engineering first.

## Approach

- **Our own code, plus licensed third-party code.** The firmware is our own C, plus any third-party code (open or
  proprietary, e.g. JieLi's SDK) whose licence lets us use and redistribute it ([THIRD-PARTY.md](THIRD-PARTY.md)).
  The stock firmware has no such licence: it is only the reference for how the hardware works (pins, timings,
  register values). The lifter and the emulator make it quick to read.
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
| 1.1 | Update-mode protocol (ping, rate, reads, container and loader checks, hand-over record) | **done** | `src/update/update.c`, tested by `tests/update_host.py` against the real flasher host |
| 1.2 | Loader store: choose the flash address, erase, write, read back and verify | **done** | `UPDATE_StoreLoader` writes the same bytes at the same addresses as the stock allocator (learnt in the emulator, `emu/loader_store.py`); checked by `tests/update_host.py` |
| 1.3 | Hand-over reset: record to 0x2FF88, close interrupts, reset into the bootloader | **done** | `port_handover` in `src/update/update_radio.c` plus `src/update/handover.S` (run from RAM), our own version of the stock steps at `0x020011E6`, watchdog off first; on a radio (2026-11-22) the stock loader started and wrote the app. Not yet: the 24 MHz system clock stock sets first (needed for 38400?) |
| 1.4 | Cable UART on the USB pins (RX 61 = D+, TX 62 = D−): pin routing and IO map | **done** (9600) | `src/driver/uart.c`; on a radio (2026-11-22) a whole update over the K1 cable at 9600 baud (48 MHz clock). Collects on the time-out/full flag, with a 1 s backstop; many retries still |
| 1.5 | Internal-flash driver: read, page program, sector/block erase, JEDEC id | **partial** | our own RAM code (`src/driver/flash.c`): ID, read, 4 KB erase, program and read-back work on a radio (2026-11-21). The update mode now uses it, and our own hand-over (`src/update/handover.S`); `tests/update_host.py` passes; not yet on a radio |
| 1.6 | Run the C update mode on the CPU emulator, then on a radio with a case-open recovery path at hand | **done** (one radio, 9600) | on a radio with the rescue route ready (2026-11-22): our update mode received the stock image and its loader, handed over, and the radio came up on stock V0.27 |
| 1.7 | Setting (menu item, and a key combo at power-on) that restarts into the chip's ROM USB download mode, so a flash backup or recovery needs no key-sending dongle ([tools/jl_usbkey](../tools/jl_usbkey/README.md)) | **todo** | how the stock code or JieLi's SDK jumps to the ROM's download mode is to find; still needs the `G M P` wires, as the K-plug carries only D− |
| 1.8 | Case-open recovery route: ROM USB download mode through the `G M P` pads | **done** (one radio) | entered with an ESP32 ([tools/jl_usbkey](../tools/jl_usbkey/README.md)); full 1 MB read, and erase, write and read-back of a test sector, then the flash equal to the backup (RE-NOTES). Full restore of all 256 sectors from the backup: done, equal byte for byte |
| 1.9 | Flasher guard for the cable route: before sending any image, check that its app keeps a working cable update mode (entered with PTT A + `=`, answers ping, rate and reads, stores the loader, hands over) and never writes PRCT (calibration) or VM (settings) outside the loader slot. If it can't prove that, refuse, or show a big warning that the radio can then only be recovered by opening the case and using the ROM USB rescue mode (`G M P` wires and a key sender, 1.8) | **partial** | `tools/uv5r_flasher.py` checks the image's app before sending: the stock update-mode code byte for byte (entry, UART, parser, record, loader store, RAM code), or our marker (`src/update/update_radio.c`) whose declared write windows must stay inside the app area and loader slot; anything else is refused. Our own update mode passes only after the big rescue warning and a typed `I CAN OPEN THE CASE` (GUI: a second warning). `python tests/flasher_guard.py`. Still to do: let `tools/mkufw.py` build flashable images of the C build once its update mode has worked on a radio |

## 2. Platform (chip and board)

| # | Item | Status | Notes |
| --- | --- | --- | --- |
| 2.1 | Start-up: clocks and PLL, cache, RAM code copy, `.bss` | **partial** | runs on a radio (2026-11-21): `src/start.S` keeps the boot record and puts the stacks at 0xD910 as stock; the CPU runs slow (clocks not set up yet). Found in stock: 24 MHz crystal, `clk_set("sys", hz)` at `sub_01E69AAC` (max 240 MHz), 24 MHz idle (RE-NOTES, peripheral sweep) |
| 2.2 | Watchdog | **partial** | runs after the bootloader; cleared with `p33_or(0x80, 0x40)` through our own P33 access, which keeps our app running on a radio. Stock writes `WDT_CON` (P33 `0x80`) = 0, then 12 early in `main` (4 s if JieLi's usual table holds) |
| 2.3 | Power: hold (PB10), off switch sense (PB11), power-off sequence | **partial** | pins known; PB11 high = off (seen on a radio); power-off in the key-test app, not yet tried on a radio |
| 2.4 | Timer tick and interrupts (vector table, `rti`), 10 ms time slice | **todo** (located) | stock tick: timer at `0x1E0500`, interrupt 5, handler `0x01E568C4`; `APP_TimeSlice10ms` 0x01E4F1AA |
| 2.5 | ADC: battery voltage and level, low-battery warning and cut-off | **partial** | battery = PB3, ADC channel 6; ADC registers and the stock scale found (RE-NOTES); `src/driver/adc.c` (polled) and `src/app/battery.c` show volts and use the radio's own thresholds (stock comparisons); low battery blinks as stock blinks its icon; PC-tested only. Cut-off: not found in stock yet |
| 2.6 | LEDs (TX red / RX green), backlight dimming and timeout | **partial** | backlight PB7; likely green RX LED = PB9, red TX LED = PR0; flashlight = PB6 (RE-NOTES). Not tried on a radio |
| 2.7 | Audio: speaker amplifier enable, routing BK4819 audio to the speaker, beeps and tones | **partial** | amp enable PB8 (high = on), shared by six audio sources in stock; RX audio needs only `BK4819_SetAF` + PB8; beeps and voice prompts use the MCU's DAC (audio block `0x1E2F00`, analog `0x1E19C0`). Not tried on a radio |
| 2.8 | External SPI flash driver (CS PA3 on SPI1) | **partial** | `src/driver/extflash.c`: read only (ID, read), bit-banged like `tools/jl_extflash`; no write or erase code on purpose; not yet on a radio |

## 3. Radio (BK4819)

| # | Item | Status | Notes |
| --- | --- | --- | --- |
| 3.1 | Register access, RX/TX switch, set frequency | **done** (basic) | `src/driver/bk4819.c`; reads and writes work on a radio (2026-11-21), with the pull-up on SDATA (PC0) as stock |
| 3.2 | Band and filter switching (VHF/UHF LNA and PA select pins), TX power levels | **partial** | found (RE-NOTES): BK4819 GPIO3 = VHF TX path, GPIO4 = UHF TX path, PC7 + GPIO5 = RX front end, five bands by frequency; TX power levels still to find (likely calibration rows) |
| 3.3 | Per-radio calibration: TX power, squelch, battery | **partial** | layout known (RE-NOTES; agrees with Codeplug Studio): PA bias by power and band, squelch thresholds and offsets; `src/store/calib.c` uses PA bias and squelch; battery, mic, trim not used yet |
| 3.4 | Squelch (RSSI, noise, glitch), S-meter | **partial** | the stock way: thresholds (the radio's own calibration) into `REG_78`, open and close on the chip's result `REG_0C` bit 1 (stock `sub_01E4DE3C`), in `src/radio/radio.c`. **Works on a radio** (2026-11-21): squelch 3 opens and stays open while a nearby radio is keyed, and closes after. Needed the pull-up on PC0 (BK4819 SDATA) as stock: without it every register read came back near zero (RSSI about -157 dBm). RSSI bar on the screen |
| 3.5 | CTCSS and DCS encode and decode, tone and code scan | **partial** | encode and decode done the stock way (`src/radio/tones.c`, `BK4819_SetCtcss` / `SetDcs` / `CssHeard`): the stock tables (51 CTCSS, 105 DCS), the stock tone encoding, the Golay word checked against the stock routine for all 105 codes, the sub-audio level from the radio's calibration, tone squelch on receive; tone and code scan as stock (chip scan `REG_51 = 0x0300`, nearest CTCSS within 0.9 Hz, DCS word in any rotation, stored by setting `0x2B`); PC-tested only |
| 3.6 | Bandwidth (wide/narrow), AM/FM demodulation, mic gain, VOX, DTMF | **partial** | wide/narrow and AM/FM (airband AM on AUTO) in `src/radio/radio.c`; DTMF decode on receive as stock (`REG_24 = 0x8A7F`, `REG_3F = 0x0800`, code in `REG_0B` bits 8-11), shown for the stock hang time (setting `0x39`); PC-tested only. Mic gain (`REG_7D` low 5 bits from calibration `0xF210`, at TX start) and VOX (the mic level on PA1, ADC channel 0, against the stock thresholds) built the stock way behind the transmit switch; PC-tested only. DTMF keys while transmitting (a held 0-9, * or # sends its pair; setting `0x09` bit 0 = heard locally) and the single tones 1000/1450/1750/2100 Hz in the driver, the stock way; PC-tested only, behind the transmit switch. PTT-ID as stock: the up code at TX start and the down code at PTT release (channel byte 13, ANI block at external `0xA000`, codes at `0xA180` / `0xA190`), with the stock wait, code and gap times; PC-tested only. In VFO mode the PTT-ID comes from setting `0x0B` (stock `sub_01E3F9D0`). Todo: the group call path, DTMF calls received (ANI match), and which key sends the 1750 Hz burst |
| 3.7 | TX/RX state machine: PTT A/B, time-out timer, busy lock, roger beep, TX inhibit outside bands | **partial** | Roger beep as stock (setting `0x17`: 1000 Hz then 850 Hz, 40 ms each, at PTT release), then squelch tail elimination (setting `0x14`: 55 Hz, or the DCS turn-off) and 400 ms more transmit, as stock; after that the receive audio stays off for setting `0x15` x 100 ms (repeater tail clear, stock `0x01E4FF94`; what setting `0x16` adds at release, `sub_01E419B6`, isn't followed). PC-tested only. `src/radio/radio.c`: PTT A starts TX only inside 144–148 / 420–450 MHz, low power, never AM; **compiled off** (`RADIO_ENABLE_TX 0`) until receive works on a radio and the PA calibration is understood; PC-tested by `tests/radio_host.py` |
| 3.8 | Dual watch, scanning (channel, frequency, priority) | **partial** | scanning (`src/radio/scan.c`): VFO over the stock scan range (settings `0x2C`–`0x2F`), channels with the scan flag, the stock resume modes TIME / CARRIER / SEARCH (setting `0x0A`); PC-tested only. **Dual watch: written but doesn't work on a radio (2026-11-21, bug)** (`src/radio/dualwatch.c`, A/B sides in `src/app/app.c`; stock setting `0x04`; our own timing, the stock one not traced); PTT-B transmits on B, PC-tested only. Priority scan todo |
| 3.9 | FM broadcast radio | **partial** | the AC695N's own FM receiver (`fm_inside`), 87.5–108 MHz, played through the DAC. Mapped (RE-NOTES): front end `0x1F1700`, software demodulation on interrupt 43 with filter tables, DAC `0x1E2F00`. Needs a front-end driver, the DSP and a DAC driver: large, and only checkable on a radio |

## 4. User interface

| # | Item | Status | Notes |
| --- | --- | --- | --- |
| 4.1 | LCD driver, rectangles, pixels, text | **done** | `src/driver/st7735s.c`, `src/ui/` |
| 4.2 | Fonts and icons as stock | **unknown** | may live in the external flash (0x0C0000 area) |
| 4.3 | Keypad scanning, long press, F-key layer, key lock | **partial** | matrix and key map done and checked on a radio (`src/driver/keyboard.c`); layers todo |
| 4.4 | Main screen: VFO/channel A and B, frequency, name, S-meter, battery, icons | **partial** | `src/ui/screen_main.c`: A and B shown together, each with badge, label, frequency, RX/TX icon, S-meter and setting tags; status bar with icons (volume, squelch, dual watch, scan, lamp, warning, battery). Icons are pixel-art text in `assets/icons/` (docs/LAYOUT.md). Checked on the PC (`tests/ui_preview.py`) and on a radio (2026-11-21) |
| 4.5 | Menu system with every stock menu item | **partial** | a first menu (scrolls): A/B, dual watch, memory, scan, scan mode, tone scan, tone-scan save, squelch, volume, step, bandwidth, modulation, RX tone, TX tone, TX power, key test, about (name, version `FW_VERSION` in `src/app/version.h`, git build id, radio kind); not saved yet |
| 4.6 | VFO entry, channel store/delete, power-on screen and messages | **partial** | 6-digit frequency entry; no channels yet (need the external-flash driver, 2.8) |

## 5. Data and compatibility

| # | Item | Status | Notes |
| --- | --- | --- | --- |
| 5.1 | Settings store compatible with stock (JieLi VM format, banks at 0x94000–0xFE000) | **unknown → partial** | VM module located (0x01E42380–0x01E42F00); format to document. The radio settings themselves are in the external flash: VFO log at `0x8010` (0x42-byte slots), settings log at `0x9010` (0x60-byte slots), CRC-16/XMODEM; read by `src/store/settings.c` (current slot from the header bitmap, as stock) and used at start-up. Writing them back the stock way is built (`SETTINGS_ENABLE_SAVE`, **off** by default: then the image has no external-flash write code), fenced to `0x8000`–`0x9FFF` with read-back; PC-tested on a simulated flash and on a dump (a save changes one slot and one header byte). Not tried on a radio |
| 5.2 | Channel memory format as stock (so existing radios keep their channels) | **partial** | record format known; `src/store/channels.c` reads the stock channels (MENU → MEMORY), checked against one radio's dump on the PC; no writing |
| 5.3 | Programming protocol in normal mode (clone read/write for CHIRP, Codeplug Studio) | **done** (PC) | `src/app/program.c`: CHIRP's UV-5R Mini protocol at 115200 on the cable (ident `PROGRAMCOLORPROU`, `F`/`M`/`SEND!`, 64-byte `R`/`W` blocks scrambled with "CO 7"). CHIRP's addresses are the external-flash ones: channels in place, 0x8000/0x9000 the current VFO and settings slots of stock's logs, 0xA000 DTMF in place. `tests/chirp_host.py` runs CHIRP's own driver, unchanged, against it, starting from a radio's flash dump: download, edit, upload, read back, no erase for an unchanged upload, nothing past 0xAFFF touched. Not yet on a radio; Codeplug Studio not tried |
| 5.4 | Bluetooth / Wireless CPS | **todo** | optional edition with JieLi blobs (docs/BLUETOOTH.md) |

## 6. Tooling and tests

Tools that work on stock-derived files produce them on your own PC, in `build/`, from your own copy of the stock
firmware: they are never in the repo.

| # | Item | Status | Notes |
| --- | --- | --- | --- |
| 6.1 | Matching build, flasher with emulated radio, image checks | **done** | |
| 6.2 | CPU emulator able to run the C build to its main screen | **partial** | boots stock part way; the C build needs no RTOS, so it is the easier target |
| 6.3 | External flash dump from a real radio | **done** | read whole (2 MB, ID `C46015`) over USB with [tools/jl_extflash](../tools/jl_extflash/README.md); layout in RE-NOTES (channels, settings, fonts, pictures; top 1 MB blank). Next: decode for 4.2 and 5.2 |
| 6.4 | Hardware confirmations: speaker amp, band switch, LEDs, ADC channel | **todo** | candidates from the stock code: amp PB8, LEDs PB9/PR0, lamp PB6, ADC PA1/PB3; a key-test build or a multimeter will settle them |
| 6.5 | Lifter: turn the stock app into C on your own PC, checked against the stock code | **done** | `python tools/lift/lift.py` with your own stock `.ufw` unpacked; output goes to your `build/lift/` ([LIFTER.md](LIFTER.md)) |
| 6.6 | Readable-C rewrite of the lifted functions with a local model (Ollama), each one tested | **done** (tool) | `python tools/lift/local_rewrite.py`; runs for hours on your PC, output in your `build/readable/` |
| 6.7 | NRFB radios (firmware V0.04): the same firmware rebuilt, app 596 bytes bigger, settings area 4 KB further on | **partial** | `tools/setup.py` unpacks it; `tools/disasm.py` and `tools/lift/lift.py --variant nrfb` work (1,974 code functions, 100% lifted); tests, emulator, builds and flasher patches are still NRF-only |
| 6.8 | Compare NRF and NRFB function by function | **done** | `tools/variant_diff.py` (needs both lifted) |

## Order of work

"Now: top priority" at the top of this page comes first. After that:

1. Section 1 (safety), then flash the C build once with a recovery path ready.
2. 2.1–2.4 and 5.1 so the radio boots, keeps time and keeps its settings.
3. 3.1–3.4, 3.7 and 2.7 for a radio that receives and transmits on one frequency.
4. 4.3–4.6 and 3.5 for everyday use; then 5.2–5.3 so programming software works.
5. The rest (scanning, dual watch, FM radio, DTMF, Bluetooth).
