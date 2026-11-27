# The radio app

`src/app/app.c` and `src/radio/radio.c` turn the key-test build into a first receiver. They are written fresh for this radio
from the stock app's hardware facts ([RE-NOTES](RE-NOTES.md), "Peripheral sweep"). The UV-K5 open firmware
(DualTachyon, Apache-2.0) was a reference for how a radio firmware is laid out and for BK4819 register meanings. No
code was copied from it.

**Checked on one radio (2026-11-21):** the screen, every key, receiving with audio, and the squelch (level 3 opens and
holds on a nearby radio, closes after). Since then also on Sean's radio: transmit (2026-11-24), the apps
(2026-11-24), FM broadcast radio (2026-11-26), and BLE SCAN, BT BEACON and BT INFO (2026-11-27). Everything else
here is PC-tested only until someone tries it; [ROADMAP.md](ROADMAP.md) has each part's state.

## What it does

| Part | Where | What |
| --- | --- | --- |
| Radio core | `src/radio/radio.c` | one VFO; band switch as stock; FM or AM (airband 108–136 MHz is AM on "AUTO"); the BK4819's own squelch (thresholds in `REG_78`, result in `REG_0C` bit 1, as stock); speaker amp, AF unmute and green LED when it opens; key beeps |
| Screen and keys | `src/app/app.c`, `src/ui/screen_main.c` | both sides at once, each with its badge, channel label, frequency, receive/transmit icon, S-meter and tags (FM/AM, W/N, power bars, shift, CTCSS/DCS); a status bar with volume, squelch, dual watch, scan, lamp, warning and battery icons; 6-digit frequency entry; menu in sections (CHANNEL, SCAN, TRANSMIT, APPS, BLUETOOTH, SYSTEM; ABOUT on the first page), items numbered on across them, a number typed on any menu page goes to that item (two digits, or one and MENU); CHANNEL has FREQUENCY (UP/DOWN steps, or 6 digits) and CH NAME (typed with the keypad letters, [below](#typing-text); MENU saves it to the channel); an RX/TX timer on the bottom line. Preview on the PC: `python tests/ui_preview.py` |
| Stock behaviours | `src/app/app.c` | from the stock settings: backlight time-out (5–20 s, first key press only lights the screen), transmit time-out (n × 15 s), busy lockout in VFO mode, key beep |
| A/B and dual watch | `src/app/app.c`, `src/radio/dualwatch.c` | two sides as stock (each with its own VFO or channel), MENU → A/B switches; with DUAL WATCH on (stock setting), the other side is checked while the active one is quiet and shown in green when it opens; PTT-A transmits on A and PTT-B on B whichever side is selected, and the selection stays (only A/B changes it); the other side shows in small text under the frequency |
| = key, monitor, 1750 Hz | `src/app/app.c` (`eq_tick`), `src/radio/radio.c` (`RADIO_Monitor`) | `=` has a press, double-press and hold action, chosen in the menu (= PRESS, = DOUBLE, = HOLD): NONE, MONITOR, SCAN, A/B, VFO/MR, W/N, POWER, LAMP, DUAL WATCH, KEY LOCK, SPECTRUM, SIG METER, SQUELCH +, SQUELCH -. Hold is MONITOR by default: the squelch is open while `=` is held. While transmitting, `=` held sends 1750 Hz. Battery on the status bar as an icon, percent or volts (MENU → BATTERY). LOCK PTT: the key lock holds PTT too. The last 10 s before the TX time-out show "TX ENDS IN 9S" with the red LED blinking |
| Apps | `src/app/apps.h`, `fmradio.c`, `spectrum.c`, `meter.c`, `chbook.c`, `sysinfo.c`, `snake.c`; screens `src/ui/screen_apps.c` | MENU → APPS, after F4HWN's apps, each its own `ENABLE_APP_*` switch: FM RADIO (64-108 MHz on the MCU's FM receiver through the DAC, `src/platform/jl_sdk/fmrx.c`; UP/DOWN 100 kHz, digits a frequency ("1015" = 101.5), `=` seek, `*`/`#` volume; not run on a radio yet), SPECTRUM (64 steps round the VFO from the BK4819's RSSI, `RADIO_ProbeRssi`; digits tune the centre, `*`/`#` step, UP/DOWN move, `=` stops the sweep and listens on the centre (`RADIO_ProbeListen`; broadcast FM is too wide for the BK4819 and sounds distorted), MENU tunes to the marked peak (marked only 10 dB over the noise), PTT transmits on its centre), SIGNAL METER (on the selected side: the title shows "MR 005" and the channel's name, or "VFO" and the frequency; VM switches, UP/DOWN step, digits tune a channel (3) or a frequency (6), through `APP_*` in apps.h; PTT transmits there and the meter stays open (`ptt_stays`); dBm, S-units, peak (`#` clears), history, `*` tick sound that speeds up with the signal), CHANNELS (the channel list with names; MENU picks one), APRS (position beacon out, packets in; `src/app/aprs.c`, ROADMAP 3.14), SUB-GHZ (315/433 MHz remote controls, receive only; `src/app/subghz.c`, ROADMAP 3.15), SYSTEM INFO, SNAKE. PTT closes an app, except the signal meter. SPECTRUM and SIG METER can also be `=` key actions. PC-checked only (radio_host covers the probe) |
| Bluetooth | `src/bt/`, `src/app/bt*.c`, `src/platform/jl_sdk/bt_le.c` | MENU → BLUETOOTH, a section of its own: BLE SCAN (devices nearby, strongest first; MENU details, 1 active/passive, 0 clear), BT BEACON (1 name, 2 iBeacon, 3 Eddystone URL; MENU on/off, UP/DOWN interval), BT SERIAL (a terminal on the BLE serial link; MENU types a line), BT CHIRP (CHIRP over Bluetooth, through `tools/ble_serial.py` on the PC), BT INFO. Bluetooth is on only while one of them is open. [BLUETOOTH.md](BLUETOOTH.md) |
| Power saving | `src/app/app.c` (`sleep_tick`), `src/radio/radio.c` (`RADIO_RxSleep`), `src/driver/st7735s.c` (`ST7735S_Sleep`) | MENU → SYSTEM → BAT SAVE (OFF, 1:1, 1:2, 1:4): after 10 s idle on the main screen the receiver listens 100 ms and is off n × 100 ms (A and B in turn with dual watch). SLEEP (OFF, 1, 5, 10, 15, 30 min, 1 h, 2 h; default 1 h), as F4HWN's SetOff, and SLEEP NOW: the backlight blinks for 10 s, then the screen goes off and the receiver listens 200 ms every 3 s; the green LED blinks every 5 s; a signal that opens the squelch, a key (not acted on) or PTT wakes it. **Low battery**: under 5 % "BATTERY LOW" and a beep every minute, and transmit is refused unless the radio was switched on with `=` held |
| DTMF | `src/radio/radio.c`, `src/driver/bk4819.c` | DTMF heard while receiving shows above the key help (`DTMF 123*`), up to 16 characters, cleared after the stock hang time (3 s and up) |
| Scanning | `src/radio/scan.c` | MENU → SCAN: the VFO over the stock scan range (400–470 MHz by default), or the channels marked for scan; resume by the stock SCAN MODE (TIME 5 s, CARRIER 2 s after the signal goes, SEARCH stops); EXIT stops it and goes back where it started, any other key stops it on what it found, UP/DOWN turn it round |
| Battery | `src/driver/adc.c`, `src/app/battery.c` | PB3 (ADC channel 6) about once a second, volts bottom right as the stock voltage screen shows them; red and blinking below the second calibrated threshold, as stock blinks its icon |
| Board I/O | `src/driver/io.c` | amp PB8, RX LED PB9, TX LED PR0, lamp PB6 (pins from the stock code, unconfirmed) |
| BK4819 | `src/driver/bk4819.c` | audio mode, volume, filter, REG_33 GPIOs, squelch registers, interrupts, CTCSS and DCS, PA, TX on, beep |
| Tones | `src/radio/tones.c` | CTCSS and DCS in the stock encoding and tables; the DCS word; tone squelch in `src/radio/radio.c`; MENU → TONE SCAN finds the tone a signal carries (kept in RX, TX or both by TSCAN SAVE, the stock setting) |
| Calibration | `src/store/calib.c` | the radio's own PA bias and squelch thresholds from the external flash (`0xF000`) |
| Stock settings | `src/store/settings.c` | the stock VFO A/B and settings logs (external flash `0x8010`, `0x9010`), read only: start-up squelch, key beep, VFO and channel mode as stock left them |
| Channels | `src/store/channels.c` | the stock channel memory (external flash `0x0000`, 999 × 32 bytes), read only |
| External flash | `src/driver/extflash.c` | read only: no erase or write code exists |
| About | `src/app/version.h`, `src/app/version.c` | MENU → ABOUT: name, version, git build id, the kind of radio the build is for |
| Key test | `src/app/keytest.c` | the old test screen, now the menu item KEY TEST (runs until switched off) |
| Typing | `src/ui/textentry.c` | one keypad typing service for every text field ([below](#typing-text)) |
| CHIRP | `src/app/program.c` | CHIRP's UV-5R Mini driver over the cable at 115200 (any screen), or over Bluetooth with BT CHIRP |
| Remote control (debug builds) | `src/app/monitor.c` | the PC reads the screen and state and presses keys over the cable ([below](#remote-control-from-the-pc-debug-builds)) |
| Update mode | `src/update/update_radio.c` | kept: PTT-A + `=` at power-on |

Settings are kept the stock way (`ENABLE_SAVE`, on): at power-on the app reads the stock app's own settings: the
squelch, key beep, each side's VFO (frequency, tones, power, width, step, shift) and channel, and our own extra bytes
(volume, FM station, APRS settings). Changes are written back once a key is let go, and at power-off. Without stock
settings it starts at 145.520 MHz in VFO mode. MENU → MEMORY switches to the radio's
own stock channels: UP/DOWN go through the filled slots, three digits pick a channel, and the channel's RX and TX
frequencies, RX and TX tones (CTCSS or DCS), power and width are used. Checked against a dump of one radio on the PC: 76 channels
decoded with their names. If the external flash doesn't answer, MEMORY stays on VFO and the status bar shows a red
warning triangle.

## Keys

Every key was checked on a radio (KEY TEST ROWS readout, then each key pressed, 2026-11-21). Codes are in
`src/driver/keyboard.h`, actions in `src/app/keys.h`:

| Key (code) | Action |
| --- | --- |
| MENU (5) | open the menu; in the menu, edit / confirm; in frequency entry, accept |
| UP (6) / DOWN (7) | step up / down; menu up / down |
| EXIT (8) | cancel entry; leave the menu |
| `*` (12) | wide / narrow; held: scan; F then `*`: tone scan |
| `#` (20) | F (the function key, as the UV-K5's): a tap turns it on and off, shown as a yellow F at the left of every top bar; held 1 s: key lock |
| A/B (3) | side A / B |
| V/M (2) | VFO / channel mode |
| flashlight (4) | on every screen: lamp off, on, strobe (as stock; strobe rate is a guess); held: the key help page for that screen, EXIT leaves it |
| 0–9 | frequency entry: six digits, MHz then kHz (`145520` = 145.520 MHz); held, or F then the digit: see below; in a text field, letters ([Typing text](#typing-text)) |

Held (half a second), or F then the key, on the main screen (after the UV-K5 open firmware's layout; ours where the
radio has no such thing): 1 step, 2 A/B, 3 VFO/MR, 4 signal meter, 5 spectrum, 6 power, 7 VOX (F then 7 only; "VOX ON" stays on the bottom line), 8 dual watch,
9 monitor, 0 FM radio, F then UP/DOWN squelch. Keys with a held job act when let go. In an app, F then UP/DOWN stand for
the app's `#` and `*` (FM volume up/down, spectrum step). F goes off after the next key on every screen.

Hold A/B, V/M or the flashlight key at power-on (PTT not held) to open KEY TEST.

## Typing text

Every text field (a channel name, the APRS call sign, a BT SERIAL line) types the same way, as on an old phone
(`src/ui/textentry.c`; PC test `python tests/textentry_host.py`):

| Key | In a text field |
| --- | --- |
| 2–9 | letters: 2 = A B C 2, 3 = D E F 3, … 7 = P Q R S 7, 9 = W X Y Z 9. Press again within 1 s for the next one; another key, or a pause, moves on |
| 0 | space, then 0 |
| 1 | marks: `. - / , ? ! ' + # * ( )`, then 1 |
| F (`#`) | letters ↔ numbers. A field starts in letters (`ABC` shows); in numbers (`123`) each key types its digit |
| `*` | delete the last character |
| UP / DOWN | change the last character through everything the field allows |
| MENU | done (save, send) |
| EXIT | cancel |

A field only offers what it can hold: a call sign takes capitals, digits and `-`; a channel name also takes a space
and `. / + # * ( )`. While typing, `#` is the field's F, not the radio's F.

## Remote control from the PC (debug builds)

Built with `ENABLE_DEBUGGING=1`, the radio answers text commands on the cable while it runs (`src/app/monitor.h`):
the screen's text, its state, key presses, memory, BK4819 registers, and the open app's own commands (APRS, SUB-GHZ,
BLE SCAN). CHIRP still works on the same cable: a monitor command is a line that starts with `!`.

1. Plug in the cable and switch the radio on.
2. Start the monitor (it owns the COM port): `python tools/radiomon.py serve --port COM7`.
3. Send commands from another window: `python tools/radiomon.py send "state"`, `"screen"`, `"key MENU"`,
   `"app help"`. `python tools/radiomon.py log` shows the end of the log (`build/radiomon.log`).
4. For an AI agent: `tools/radio_mcp.py` offers the same as MCP tools (registered in `.mcp.json` as `uv5r-radio`).
5. Stop the monitor before CHIRP or the flasher use the port.

Sub-GHz helpers: `python tools/subget.py remote.sub` saves the radio's last SUB-GHZ capture as a Flipper Zero `.sub`
file (through the monitor); `python tools/sdr_ook.py record` records a remote with SDRangel and an RTL-SDR and
decodes it on the PC, to compare with the radio's own capture.

## Transmit

On by default (`ENABLE_TX`, features.mk; transmits on a radio since 2026-11-24). Mic gain comes from the radio's
calibration and VOX works as stock (stock VOX setting; the mic level on PA1). Transmit is guarded:

- only where the TX lock allows (`src/radio/txlock.h`): HAM by default (144–148 and 420–450 MHz in the US), or LOCKED, GMRS / PMR446 / UHF CB by region, HAM + that service, UNLOCKED. Switch on with `=` held: the menu opens at its hidden items, REGION and TX LOCK. Never AM, never airband or 200–300 MHz (no TX path there);
- the PA bias and gain come from the radio's own calibration (High or Low, by frequency), as the stock app does it;
  no calibration for a frequency (or none at all) means no transmit;
- a channel's own TX frequency is checked the same way, and receive-only channels never transmit;
- TX path through BK4819 GPIO3 (VHF) or GPIO4 (UHF), as stock.

Turn it on only after receive works on a radio.

## Tests on the PC

```powershell
python tests/radio_host.py
```

This builds `src/radio/radio.c` with stand-in drivers, with transmit compiled off and then on. It checks the band-switch
outputs, the squelch opening and closing (fallback and calibrated), AM on airband, the PA rows and gain bytes, the
transmit guard at the band edges, channel TX frequencies, receive-only channels, and channel-record decoding. The
calibration in the test is made up: no radio's own data is kept in the repo.

## First test on a radio (rescue route only)

Only with the case-open rescue route ready and a full backup made (memory: `uv5r_backup.ps1`). The image is
`build/open-uv5r-mini-flash.bin`, made with `make ufw` and `tools/mkflashimg.py` from your own backup. It only
changes the app area, `0x4000`–`0x7C000`. Put it on with `uv5r_flash_ours.ps1`, and back with `uv5r_restore.ps1`.

1. Switch on. You should see the frequency or channel the stock firmware was last on, and a signal bar.
2. The status bar should not show a red warning triangle. If it does, the external flash didn't answer.
3. Press each key and check it does what the Keys table says.
4. If a key is wrong, hold A/B at power-on to open KEY TEST and note its ROWS readout.
5. Set squelch 0 (MENU → SQUELCH). You should hear noise from the speaker, and the green LED should light.
6. Set squelch 3. Key a second radio on 145.520: the speaker should open, then close when it stops.
7. Press the flashlight key. The lamp should toggle.
8. MENU → MEMORY. UP/DOWN should walk your stock channels, with their names.
9. Press PTT on a frequency the TX lock allows (or with a dummy load): the red LED lights. Outside the allowed bands
   the bottom line says why it won't transmit.
10. Look at the bottom right: the battery volts. Compare with the stock power-on voltage screen. If it shows
    nothing, the ADC didn't answer.
11. Set MENU → RX TONE to the tone a second radio sends (e.g. 88.5). The speaker should open only with that tone.
    Then MENU → TONE SCAN and key the second radio: it should beep and show the tone. (A DCS code may show as its
    alias, e.g. D023I as D047N; stock does the same.)
12. Send DTMF from a second radio (e.g. `123#`). It should show as `DTMF 123#` at the bottom, then clear.
13. MENU → SCAN. `SCAN` shows top right and the frequency or channel moves; it stops on a signal (green), and any
    key ends it.
14. Look under the frequency: the other side (B or A). MENU → A/B swaps them. Key a second radio on the other
    side's frequency: it should show in green and play, then go back to the active side.
15. Turn the power knob off. The radio should switch off.
16. To test the cable route without writing: switch on holding PTT-A + `=`. Run `tools/uart_ping.py --port COM4`.

## Still to do

- **Confirm on a radio:** the function keys, the amp, the LEDs and the lamp.
- **Persistence:** on (`ENABLE_SAVE`); every erase and program is refused outside `0x0000`–`0xAFFF` (channels,
  settings, DTMF) and read back. Not yet tried on a radio for every item; keep a fresh external-flash backup.
- **Squelch:** the calibrated thresholds go to `REG_78` too; the BK4819's own squelch status could replace the RSSI
  compare once a radio shows which works better.
- **Not built yet:** priority scan, the timer tick instead of the delay loop, a clock, voice prompts.
