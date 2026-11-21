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
| Calibration | `app/calib.c` | the radio's own PA bias and squelch thresholds from the external flash (`0xF000`) |
| Channels | `app/channels.c` | the stock channel memory (external flash `0x0000`, 999 × 32 bytes), read only |
| External flash | `driver/extflash.c` | read only: no erase or write code exists |
| Key test | `app/keytest.c` | the old test screen, now the menu item KEY TEST (runs until switched off) |
| Update mode | `app/update_radio.c` | kept: PTT-A + `=` at power-on |

Settings are not saved yet: each power-on starts at 145.520 MHz in VFO mode. MENU → MEMORY switches to the radio's
own stock channels: UP/DOWN go through the filled slots, three digits pick a channel, and the channel's RX and TX
frequencies, CTCSS on transmit, power and width are used. Checked against a dump of one radio on the PC: 76 channels
decoded with their names. If the external flash doesn't answer, MEMORY stays on VFO and the status line shows
`NOCAL`.

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

1. Switch on. You should see 145.52000 and a signal bar.
2. The status line should not say `NOCAL`. If it does, the external flash didn't answer.
3. Press each key on the 4th column and note what happens.
4. Open KEY TEST from the menu if the keys are wrong. Write down the code for MENU, UP, DOWN and EXIT.
5. Set squelch 0 (MENU → SQUELCH). You should hear noise from the speaker, and the green LED should light.
6. Set squelch 3. Key a second radio on 145.520: the speaker should open, then close when it stops.
7. Press `#`. The lamp should toggle.
8. MENU → MEMORY. UP/DOWN should walk your stock channels, with their names.
9. Press PTT. It should beep and say `NO TX` (transmit is compiled off).
10. Turn the power knob off. The radio should switch off.
11. To test the cable route without writing: switch on holding PTT-A + `=`. Run `tools/uart_ping.py --port COM4`.

## Still to do

- **Confirm on a radio:** the function keys, the amp, the LEDs and the lamp.
- **Persistence:** our own settings. Not in the external flash: writing there risks the channels and calibration.
- **Squelch:** the calibrated thresholds go to `REG_78` too; the BK4819's own squelch status could replace the RSSI
  compare once a radio shows which works better.
- **Not built yet:** CTCSS/DCS on receive, dual watch and scanning, battery level, the timer tick instead of the
  delay loop, and FM broadcast radio.
