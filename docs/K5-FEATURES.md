# Features to port from the UV-K5 / UV-K1 custom firmware

Every feature of F4HWN's **Fusion** firmware for the UV-K1 and UV-K5 V3
([armel/uv-k1-k5v3-firmware-custom](https://github.com/armel/uv-k1-k5v3-firmware-custom), read 2026-11-24; built on
egzumer's, OneOfEleven's, fagci's and DualTachyon's work), and where this project stands on each.

**Why it ports well:** those radios use the same RF chip, the BK4819. Most radio features are BK4819 register work,
which carries over. What doesn't carry over directly:

| Them | Us | What it means for a port |
| --- | --- | --- |
| PY32F071 (ARM) | AC695N (pi32v2) | rewrite the MCU parts; the C logic ports |
| 128×64 mono LCD | 128×128 colour ST7735S | screens are redrawn, not copied |
| 2 MB external flash | 2 MB external flash (GT25Q16A) | same room; layouts differ |
| USB-C serial | cable UART on the USB pins | UV Studio links need our own protocol |
| no Bluetooth, no FM chip | built-in Bluetooth and FM receiver | ours can do more here |

**Licence:** Apache-2.0, like the code we already learn from. Code copied in keeps its headers and NOTICE and gets
a row in [THIRD-PARTY.md](THIRD-PARTY.md); code rewritten from reading it needs none.

**Status:** **have** = in our firmware; **partial** = some of it; **todo** = not started; **n/a** = not for
this radio (reason given). ROADMAP rows in brackets.

## Radio and signal handling

| # | Feature | Status | Notes |
| --- | --- | --- | --- |
| 1.1 | Output power levels: Low 1-5 (20 mW to 1 W), Mid, High, User (`SetPwr`) | partial | we have Low and High from stock calibration [3.2]; more levels need the PA bias table extended |
| 1.2 | S-meter calibrated to IARU Region 1 (S0 to S9+), Classic and Tiny styles | partial | we draw an RSSI bar [3.4]; calibration to S-units todo |
| 1.3 | Narrow FM bandwidth: 12.5 or 6.25 kHz (`SetNFM`) | partial | wide and narrow (12.5) only |
| 1.4 | Per-channel TX lock | partial | we have RX ONLY channels; a per-channel lock flag todo |
| 1.5 | RX audio volume in software (`SetVol`) | have | VOLUME menu item (BK4819 AF gain) |
| 1.6 | RX audio profiles for FM and AM (`SetRxA`), noise gate (`SetRxG`) | todo | BK4819 AF filter and compander registers |
| 1.7 | Regional frequency lock: amateur, PMR446, FRS, GMRS, MURS | partial | TX LOCK has HAM, GMRS, PMR446, UHF CB by region [3.7]; FRS and MURS todo |
| 1.8 | Battery profiles 1600 / 2200 / 3500 mAh, voltage calibration from the menu | partial | battery bars from the stock calibration; profiles and percentage todo |
| 1.9 | SSB demodulation (USB/LSB) | todo | BK4819 registers, as fagci's; needs a fine step |
| 1.10 | AM fix (gain control for AM) | todo | Fusion removed its old one; worth a look for airband |
| 1.11 | More squelch sensitivity, better squelch and S-meter behaviour | partial | the stock thresholds today [3.4] |
| 1.12 | More frequency steps | partial | 2.5 to 100 kHz; 8.33 kHz airband and finer SSB steps todo |
| 1.13 | Monitor (squelch open while held) | todo | a key action |
| 1.14 | 1750 Hz tone burst | partial | in the driver; no key sends it yet [3.6] |
| 1.15 | DTMF calling, scrambler | partial | DTMF send and decode, PTT-ID have; scrambler todo (Fusion turns both off) |

## Watching and scanning

| # | Feature | Status | Notes |
| --- | --- | --- | --- |
| 2.1 | Full Watch: VFO A, VFO B and two priority channels (`RxMode`) | partial | dual watch A/B [3.8]; priority channels todo |
| 2.2 | Respond on the channel that opened, or always TX on the main VFO | todo | |
| 2.3 | 24 named scan lists + ALL + MIX, chosen while scanning (`00`-`25`) | partial | one scan flag per channel (stock); lists need room in our channel bytes |
| 2.4 | Long-press MENU while scanning excludes the channel; up to 64 frequency exclusions | todo | |
| 2.5 | Fast scan, about 150 frequencies per second | todo | ours hops about every 60 ms |
| 2.6 | Scan display: progress, RSSI, CTCSS/DCS heard | partial | SCAN icon and frequency; the rest todo |
| 2.7 | Scan resume modes (`SetScn`, `ScnRev`) | have | TIME, CARRIER, SEARCH (stock setting) |
| 2.8 | Scan state restored at start-up | todo | |
| 2.9 | EXIT stops a scan and goes back to where it started; MENU keeps the last find | todo | |
| 2.10 | CTCSS/DCS scan from the tone menu item | have | MENU → TONE SCAN |
| 2.11 | Scan-list toggle and assign shortcuts (long `*`, long `5`) | todo | |

## Spectrum analyser (fagci)

| # | Feature | Status | Notes |
| --- | --- | --- | --- |
| 3.1 | Spectrum view (F+5) with channel names, freeze, persistent settings | todo | BK4819 RSSI sweep; the colour screen makes it nicer |
| 3.2 | Restore the spectrum at start-up | todo | |

## Screen and controls

| # | Feature | Status | Notes |
| --- | --- | --- | --- |
| 4.1 | Display modes: MAIN ONLY, DUAL, FULL, CROSS | partial | we always show both sides |
| 4.2 | RX activity on the active VFO, last-RX indication, RX and TX timers (`SetTmr`) | partial | RX/TX icons; timers and last RX todo |
| 4.3 | RX LED blink option | todo | |
| 4.4 | Squelch, step, CTCSS/DCS shown on the VFO screen | have | squelch on the status bar, tones as tags |
| 4.5 | Channel name + frequency display | have | name large, number and frequency small (2026-11-24) |
| 4.6 | Channel name editing in the menu | todo | names only from CHIRP today [4.6] |
| 4.7 | Better frequency and channel entry | partial | 6 digits, 3-digit channel |
| 4.8 | Battery percentage or voltage on the status bar | todo | bars only |
| 4.9 | Mic level bar while transmitting; audio level display | todo | the mic ADC is read for VOX already |
| 4.10 | Backlight: longer times, dimming, smooth fade, quick min/max (F+8, F+9) | partial | stock 5-20 s or always; dimming needs a PWM pin, unknown |
| 4.11 | Contrast and inverted display (`SetCtr`, `SetInv`) | n/a / todo | contrast is for mono LCDs; inverted colours could be a theme |
| 4.12 | Power-on message and start-up logo (`PonMsg`) | todo | [4.6]; stock may keep a picture in external flash [4.2] |
| 4.13 | System info pages: version, battery, flash and RAM use, QR codes | partial | ABOUT page has version and build |
| 4.14 | Menu index shown while editing | have | NN/MM in the corner |
| 4.15 | Reordered, renamed menu entries | have | our own order |
| 4.16 | Navigation layout per radio model (`SetNav`) | n/a | one keypad here |
| 4.17 | Key lock: keys only, or keys and PTT (`SetLck`) | partial | # held locks keys; PTT always works |
| 4.18 | F-key layer and long-press actions, assignable short/long actions per key | partial | # long press locks; no F layer or assignable actions yet [4.3] |
| 4.19 | F+UP / F+DOWN squelch, saved; F+F1 / F+F2 step | todo | |
| 4.20 | Quick actions: RX mode, main VFO only, wide/narrow, 1750, mute, audio profile, max power, offset removal | partial | `*` toggles wide/narrow; the rest todo |
| 4.21 | Long-press EXIT restores the full VFO state | todo | |
| 4.22 | PTT modes: Classic or OnePush (`SetPTT`) | todo | |
| 4.23 | Time-out and end-of-transmission alerts: sound, visual, both (`SetTOT`, `SetEOT`) | partial | TOT ends TX with a message; alert choices todo |
| 4.24 | Auto deep-sleep after a time (`SetOff`) | todo | needs the chip's sleep modes |
| 4.25 | Audio scope during RX and TX | todo | |
| 4.26 | Clock on the screen | todo | ours, not Fusion's: ROADMAP 4.7 |

## Memory, configuration and firmware

| # | Feature | Status | Notes |
| --- | --- | --- | --- |
| 5.1 | Multiconfig: banks of channels and settings, switched with `SetCfg` | todo | external flash has room; must not break stock's layout [5.1, 5.2] |
| 5.2 | Multiboot: firmware slots in external flash, restored at power-on | todo / risky | needs our own boot step before the app; rule 3 and the update mode come first |
| 5.3 | Overlay apps run from external flash in a RAM overlay | todo | AC695N has more RAM; an app slot format of our own |
| 5.4 | RF Log: RX/TX history (frequency, channel, length, S-meter, battery) in external flash | todo | needs a clock for times (ROADMAP 4.7) |
| 5.5 | Persisted VFO state and squelch | have | saved at once [5.1] |

## Connectivity

| # | Feature | Status | Notes |
| --- | --- | --- | --- |
| 6.1 | CHIRP | have | stock-compatible protocol, CHIRP's UV-5R Mini driver [5.3] |
| 6.2 | Live screen mirror, screenshots and remote keys over the cable (K5Viewer / UV Studio) | todo | our own protocol; the colour screen needs more bandwidth (RGB565, 32 KB a frame) |
| 6.3 | Calibration dump and restore over the cable | todo | we can read it; writing it back needs care (rule 6) |
| 6.4 | Firmware flashing from a browser | todo | our update mode speaks the stock protocol; a Web Serial page could drive it |
| 6.5 | AirCopy: radio-to-radio over the air, and cable copy | todo | BK4819 FSK; both radios run ours |
| 6.6 | BEAM: send a channel to another radio | todo | as AirCopy |
| 6.7 | KISS TNC over the cable (APRSdroid) | todo | needs the AFSK modem (7.x) |
| 6.8 | Bluetooth | n/a for them | ours has it: [BLUETOOTH.md](BLUETOOTH.md) |

## Apps (overlay apps in Fusion)

Each needs a demodulator or a game loop; most are a few kilobytes of logic plus a screen. Grouped by what they need.

| # | App | Status | Needs |
| --- | --- | --- | --- |
| 7.1 | Broadcast FM | partial | ours: the AC695N's own FM receiver [3.9] |
| 7.2 | Fox Hunt (S-meter, history graph, attenuation, Geiger audio) | todo | RSSI; BK4819 attenuation |
| 7.3 | Beacon (Morse ID, timed TX, safe READY screen) | todo | CW keying through the BK4819; TX lock applies |
| 7.4 | Signal Finder, Spectrum3D | todo | spectrum sweep (3.1) |
| 7.5 | ToneCall | todo | tone generator (have) |
| 7.6 | APRS RX, TX, Edit, DIGI (Bell 202 AFSK 1200, AX.25) | todo | AFSK modem; also a time source for the clock (ROADMAP 4.7) |
| 7.7 | POCSAG RX | todo | FSK demodulation |
| 7.8 | ACARS RX (2400 bit/s AM-MSK) | todo | AM audio sampled fast enough |
| 7.9 | AIS RX (discriminator at 38.4 kHz) | todo | raw discriminator samples from the BK4819 |
| 7.10 | ATIS, ATIS DSC RX | todo | FSK / DSC decoding |
| 7.11 | Radiosonde RX: RS41, M10, M20; GeoNav (bearing to a sonde) | todo | 4800 baud GFSK, 400-406 MHz |
| 7.12 | EPIRB 406, Scan 406 | todo | 406 MHz, 400 bit/s BPSK; may be beyond the receiver |
| 7.13 | CW Decode, CW Keyer, CW Mem, CW Mem Edit, CW Trainer, CW USB | todo | CW TX keying, audio tone detection |
| 7.14 | SSTV | todo | audio sampling; the colour screen suits it |
| 7.15 | AF Scope and AF Spectro (oscilloscope, tuner, spectrum, sonogram) | todo | audio samples through the MCU's ADC |
| 7.16 | ChannelBook (browse channels by list, letter, number range) | todo | |
| 7.17 | QR Map, Resistor, System Info, Flash Map | todo | small tools |
| 7.18 | Games and demos: Breakout, Cube3D, Hell Maze, Minesweeper, Plasma, Rapid Roll, Snake, Sokoban, Space Impact, Tetris | todo | our own versions; colour helps |

## Suggested order

1. Things that make it a better daily radio, all small: battery percentage (4.8), monitor key (1.13), F+UP/DOWN
   squelch (4.19), 1750 Hz key (1.14), RX/TX timers (4.2), channel name editing (4.6), EXIT restores (2.9, 4.21).
2. Signal: S-meter in S-units (1.2), more power levels (1.1), 6.25 kHz narrow (1.3), priority watch (2.1).
3. Scanning: fast scan (2.5), scan lists (2.3), exclusions (2.4).
4. Spectrum analyser (3.1): the most-used extra on the K5.
5. SSB (1.9) and RX audio profiles (1.6).
6. Data: an AFSK modem, then APRS RX (7.6, also a clock source), then POCSAG and the rest.
7. Cable features: screen mirror (6.2), calibration backup (6.3).
8. Bigger structural pieces last: Multiconfig (5.1), apps from external flash (5.3), Multiboot (5.2).
