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
| Board I/O | `driver/io.c` | amp PB8, RX LED PB9, TX LED PR0, lamp PB6 (pins from the stock code, unconfirmed) |
| BK4819 | `driver/bk4819.c` | audio mode, volume, filter, REG_33 GPIOs, squelch registers, interrupts, sub-tone, PA, TX on, beep |
| Key test | `app/keytest.c` | the old test screen, now the menu item KEY TEST (runs until switched off) |
| Update mode | `app/update_radio.c` | kept: PTT-A + `=` at power-on |

Settings are not saved yet: each power-on starts at 145.520 MHz.

## Keys

The digits are certain. The function keys are **guesses** (`app/keys.h`) until someone presses each one in KEY TEST
and notes its code:

| Key (code) | Guess | Action |
| --- | --- | --- |
| 4th column, top (12) | MENU | open the menu; in the menu, edit / confirm; in frequency entry, accept |
| 4th column, 2nd (16) | UP | step up; menu up |
| 4th column, 3rd (20) | DOWN | step down; menu down |
| 4th column, bottom (8) | EXIT | cancel entry; leave the menu |
| `*` (5) | | wide / narrow |
| `#` (7) | | lamp |
| 0–9 | | frequency entry: six digits, MHz then kHz (`145520` = 145.520 MHz) |

## Transmit

Built in but **compiled off** (`RADIO_ENABLE_TX 0` in `app/radio.h`). With it on:

- only 144–148 and 420–450 MHz, never AM, never airband or 200–300 MHz (no TX path there);
- low power only, a fixed PA bias of `0x40`. The stock app takes the bias from the radio's calibration in the
  external flash at `0xF000`, and which row is which isn't known yet (roadmap 3.3);
- TX path through BK4819 GPIO3 (VHF) or GPIO4 (UHF), as stock.

Turn it on only after receive works on a radio and the calibration rows are understood.

## Tests on the PC

```powershell
python tests/radio_host.py
```

This builds `app/radio.c` with stand-in drivers, with transmit compiled off and then on. It checks the band-switch
outputs, the squelch opening and closing, AM on airband, and the transmit guard at the band edges.

## Still to do

- **Confirm on a radio:** the function keys, the amp, the LEDs and the lamp.
- **Persistence:** settings and channels. Channels are in the external flash (32-byte records at `0x0000`), which needs
  an external-flash driver first (roadmap 2.8).
- **The BK4819's own squelch:** use it in place of the RSSI-only one, with thresholds from the calibration rows.
- **Not built yet:** CTCSS/DCS on receive, dual watch and scanning, battery level, the timer tick instead of the
  delay loop, and FM broadcast radio.
