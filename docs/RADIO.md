# The radio app

`app/app.c` and `app/radio.c` turn the key-test build into a first receiver. They are written fresh for this radio
from the stock app's hardware facts ([RE-NOTES](RE-NOTES.md), "Peripheral sweep"). The UV-K5 open firmware
(DualTachyon, Apache-2.0) was a reference for how a radio firmware is laid out and for BK4819 register meanings. No
code was copied from it.

**Not tried on a radio yet.** Nothing here is known to work until someone runs it.

## What it does

| Part | Where | What |
| --- | --- | --- |
| Radio core | `app/radio.c` | one VFO; band switch as stock; FM or AM (airband 108–136 MHz is AM on "AUTO"); software squelch on the RSSI; speaker amp, AF unmute and green LED when it opens; key beeps |
| Screen and keys | `app/app.c` | frequency, RSSI bar and dBm, squelch, volume, bandwidth, step and CTCSS; 6-digit frequency entry; menu |
| Stock behaviours | `app/app.c` | from the stock settings: backlight time-out (5–20 s, first key press only lights the screen), transmit time-out (n × 15 s), busy lockout in VFO mode, key beep |
| A/B and dual watch | `app/app.c`, `app/dualwatch.c` | two sides as stock (each with its own VFO or channel), MENU → A/B switches; with DUAL WATCH on (stock setting), the other side is checked while the active one is quiet and shown in green when it opens; PTT-B transmits on B; the other side shows in small text under the frequency |
| DTMF | `app/radio.c`, `driver/bk4819.c` | DTMF heard while receiving shows above the key help (`DTMF 123*`), up to 16 characters, cleared after the stock hang time (3 s and up) |
| Scanning | `app/scan.c` | MENU → SCAN: the VFO over the stock scan range (400–470 MHz by default), or the channels marked for scan; resume by the stock SCAN MODE (TIME 5 s, CARRIER 2 s after the signal goes, SEARCH stops); any key stops it, UP/DOWN turn it round |
| Battery | `driver/adc.c`, `app/battery.c` | PB3 (ADC channel 6) about once a second, volts bottom right as the stock voltage screen shows them; red and blinking below the second calibrated threshold, as stock blinks its icon |
| Board I/O | `driver/io.c` | amp PB8, RX LED PB9, TX LED PR0, lamp PB6 (pins from the stock code, unconfirmed) |
| BK4819 | `driver/bk4819.c` | audio mode, volume, filter, REG_33 GPIOs, squelch registers, interrupts, CTCSS and DCS, PA, TX on, beep |
| Tones | `app/tones.c` | CTCSS and DCS in the stock encoding and tables; the DCS word; tone squelch in `app/radio.c`; MENU → TONE SCAN finds the tone a signal carries (kept in RX, TX or both by TSCAN SAVE, the stock setting) |
| Calibration | `app/calib.c` | the radio's own PA bias and squelch thresholds from the external flash (`0xF000`) |
| Stock settings | `app/settings.c` | the stock VFO A/B and settings logs (external flash `0x8010`, `0x9010`), read only: start-up squelch, key beep, VFO and channel mode as stock left them |
| Channels | `app/channels.c` | the stock channel memory (external flash `0x0000`, 999 × 32 bytes), read only |
| External flash | `driver/extflash.c` | read only: no erase or write code exists |
| Key test | `app/keytest.c` | the old test screen, now the menu item KEY TEST (runs until switched off) |
| Update mode | `app/update_radio.c` | kept: PTT-A + `=` at power-on |

Settings are not saved yet. At power-on the app reads the stock app's own settings (read only): the squelch, key
beep, the active side's VFO (frequency, tones, power, width, step, shift) and, if that side was in channel mode,
channel mode at the first filled slot (which channel each side was on isn't known yet). Without them it starts at
145.520 MHz in VFO mode. MENU → MEMORY switches to the radio's
own stock channels: UP/DOWN go through the filled slots, three digits pick a channel, and the channel's RX and TX
frequencies, RX and TX tones (CTCSS or DCS), power and width are used. Checked against a dump of one radio on the PC: 76 channels
decoded with their names. If the external flash doesn't answer, MEMORY stays on VFO and the status line shows
`NOCAL`.

## Keys

Every key was checked on a radio (KEY TEST ROWS readout, then each key pressed, 2026-11-21). Codes are in
`driver/keyboard.h`, actions in `app/keys.h`:

| Key (code) | Action |
| --- | --- |
| MENU (5) | open the menu; in the menu, edit / confirm; in frequency entry, accept |
| UP (6) / DOWN (7) | step up / down; menu up / down |
| EXIT (8) | cancel entry; leave the menu |
| `*` (12) | wide / narrow |
| A/B (3) | side A / B |
| V/M (2) | VFO / channel mode |
| flashlight (4) | lamp on / off |
| 0–9 | frequency entry: six digits, MHz then kHz (`145520` = 145.520 MHz) |

Hold A/B, V/M or the flashlight key at power-on (PTT not held) to open KEY TEST.

## Transmit

Built in but **compiled off** (`RADIO_ENABLE_TX 0` in `app/radio.h`). With it on (and only then), mic gain comes from
the radio's calibration and VOX works as stock (stock VOX setting; the mic level on PA1). Transmit is guarded:

- only 144–148 and 420–450 MHz, never AM, never airband or 200–300 MHz (no TX path there);
- the PA bias and gain come from the radio's own calibration (High or Low, by frequency), as the stock app does it;
  no calibration for a frequency (or none at all) means no transmit;
- a channel's own TX frequency is checked the same way, and receive-only channels never transmit;
- TX path through BK4819 GPIO3 (VHF) or GPIO4 (UHF), as stock.

Turn it on only after receive works on a radio.

## Tests on the PC

```powershell
python tests/radio_host.py
```

This builds `app/radio.c` with stand-in drivers, with transmit compiled off and then on. It checks the band-switch
outputs, the squelch opening and closing (fallback and calibrated), AM on airband, the PA rows and gain bytes, the
transmit guard at the band edges, channel TX frequencies, receive-only channels, and channel-record decoding. The
calibration in the test is made up: no radio's own data is kept in the repo.

## First test on a radio (rescue route only)

Only with the case-open rescue route ready and a full backup made (memory: `uv5r_backup.ps1`). The image is
`build/open-uv5r-mini-flash.bin`, made with `make ufw` and `tools/mkflashimg.py` from your own backup. It only
changes the app area, `0x4000`–`0x7C000`. Put it on with `uv5r_flash_ours.ps1`, and back with `uv5r_restore.ps1`.

1. Switch on. You should see the frequency or channel the stock firmware was last on, and a signal bar.
2. The status line should not say `NOCAL`. If it does, the external flash didn't answer.
3. Press each key and check it does what the Keys table says.
4. If a key is wrong, hold A/B at power-on to open KEY TEST and note its ROWS readout.
5. Set squelch 0 (MENU → SQUELCH). You should hear noise from the speaker, and the green LED should light.
6. Set squelch 3. Key a second radio on 145.520: the speaker should open, then close when it stops.
7. Press the flashlight key. The lamp should toggle.
8. MENU → MEMORY. UP/DOWN should walk your stock channels, with their names.
9. Press PTT. It should beep and say `NO TX` (transmit is compiled off).
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
- **Persistence:** saving the stock settings and VFO logs is written (`SETTINGS_ENABLE_SAVE` in `app/settings.h`) but
  **off**: with it off the image has no external-flash write code. On, every erase and program is refused outside
  `0x8000`–`0x9FFF` (between the channels and the calibration) and read back. Only turn it on with the radio owner's
  agreement and a fresh external-flash backup.
- **Squelch:** the calibrated thresholds go to `REG_78` too; the BK4819's own squelch status could replace the RSSI
  compare once a radio shows which works better.
- **Not built yet:** priority scan, low-battery warning, the timer tick instead of the
  delay loop, and FM broadcast radio.
