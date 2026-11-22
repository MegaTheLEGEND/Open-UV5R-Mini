# Reverse-engineering notes (stock v0.27, `app.bin` at 0x01E00100)

Addresses are from `tools/disasm.py` output for stock firmware `stock-nrf-v0.27.ufw`.

## RF chip: Beken BK4819 (same family as the UV-K5)

Evidence:

- **Bit-banged 3-wire SPI** in exactly the BK4819 framing: an 8-bit register address (bit 7 = read),
  then 16 data bits MSB first.
  - `sub_01E409C4` = `BK4819_ReadRegister(reg)`: ORs `0x80` into the address, clocks 16 bits in
    (mask `0x8000` shifted right), returns `u16`.
  - `sub_01E3E0B2` = `BK4819_WriteRegister(reg, value)`: sends `reg`, `value >> 8`, `value & 0xFF`.
  - `sub_01E3E056` = write one byte on the bus, `sub_01E3E042(5)` = short bus delay.
- `sub_01E4ECB4` reads REG_69, REG_6A, then REG_68, matching egzumer's
  `BK4819_GetCxCSSScanResult()` (CDCSS high/low, then CTCSS scan result, `& 0x1FFF`).
- `sub_01E40AA0` writes REG_47 = `0x6040 | table[af]` (`0x6640` in one mode), matching `BK4819_SetAF()`.
- Registers written (count of call sites):
  `00 02 07 08 09×16 10 11 12 13 14 19 1F 21 24 26 28 2A 30 32 33 36 37 38 39 3E 3F 40 43 47 48 49 4F 51 52 54 55 5C 5D 70 71 72 75 77 78 7B 7D`
  (16 writes to REG_09 = the AGC gain table that BK4819 init writes).
- Registers read: `0B 0C 0D 0E 24 30 33 3F 40 52 67 68 69 6A 70 7D`
  (REG_0C status/interrupts, REG_67 RSSI, REG_68–6A tone scan results).

Most of egzumer's `driver/bk4819.c` should therefore carry over almost unchanged; only the GPIO layer differs.

### BK4819 pins (JieLi GPIO numbers, via `sub_01E0019C(pin)` → port registers)

| Signal | GPIO number | Port pin (if JieLi numbering PA=0, PB=16, PC=32) |
| --- | --- | --- |
| SCN (chip select) | 33 | PC1 |
| SCK | 10 | PA10 |
| SDATA (bidirectional) | 32 | PC0 |

Port register layout as used: `+0` OUT, `+4` IN, `+8` DIR (1 = input). `sub_01E0019C(pin)` returns the
port block for `pin` from a table at 0x01E00664 (`pin >> 4` selects the port, valid for pin ≤ 56).
The 0x1E0200 read between pin writes looks like a write-flush/barrier read.

## Tone tables

| Table | Address | Format |
| --- | --- | --- |
| CTCSS | 0x01E662A6 (code indexes from 0x01E662A2/A4) | `u16` Hz×10: 670, 693, 719, 744, 770, … (51 entries) |
| DCS | 0x01E667D0 | `u16` octal codes: 023, 025, 026, 031, … (105 entries) |

- `sub_01E4ED34`: nearest CTCSS lookup (tolerance 9, i.e. ±0.9 Hz), stores the result at RAM 0x198C0+492.
- `sub_01E4EE64`: DCS code match over 105 entries, including inverted codes (bit 22 rotate), using the
  Golay encoder `sub_01E4D81E`.

## Strings and UI

- Menu strings are stored in pairs (English, then GBK Chinese), each prefixed with an attribute byte `0x07`.
- Version strings near 0x01E655A0: `%s NRF`, `5RMINI`, `VER:%s`, `Radio:`, `NOMENU`.

## First boot of our firmware on a radio (2026-11-21)

Written to the internal flash over the ROM download mode (`tools/mkflashimg.py`), one radio (NRF V0.27):

- **Our key-test app runs and draws on the screen**: keys, PTT, BK4819 `REG_00` = `0x0008`, RSSI.
- **`0x02000000` is not a mask ROM.** The stock app copies 53,492 bytes of its own code from flash `0x01E6AAAC`
  to RAM `0` at start-up and runs it through the `0x02000000` alias. Its "ROM" helpers (P33 access, flash
  driver, interrupt masking, the update hand-over) live there. Calling them without that copy ran into whatever was
  in RAM and restarted the chip. Reading `0x02000000` over USB or with CPU loads returned that RAM, not code.
- **P33 interface** at `0x1E3E08` (+0 CON, +4 DAT), from the stock routine at RAM `0x58`/`0xA0`: CON bit 0 selects,
  bit 8 picks the bank for addresses with bit 15 set (`0x8099`); a byte goes out with DAT = byte, CON |= 0x10,
  wait while CON bit 1; read = `((a>>8)&3)|0x80`, `a&0xFF`, dummy `0x5E`; write = `(a>>8)&3`, `a&0xFF`, value;
  or = first byte `| 0x20`. Our version is in `bsp/ac695n.h`.
- **Speed** after the bootloader: about 0.75 turns of a volatile delay loop per µs.
- **The watchdog runs after the bootloader.** Without clears our app restarted at the same point each time;
  with `p33_or(0x80, 0x40)` through our own P33 access it **runs stably**.
- PB11 reads **high** when the power switch is turned off (seen on a radio, 2026-11-21: our first screen showed it the wrong way round; stock `sub_01E4C978` tests the same bit). The key-test app now powers off after 0.2 s high.
- The cable update mode still calls the stock RAM-code flash and hand-over routines, so it **does not work yet**.

## Startup

See [FIRMWARE.md](FIRMWARE.md#appbin).

## LCD: Sitronix ST7735S, 128×128, RGB565

Driver functions:

| Function | Role |
| --- | --- |
| `sub_01E3E444(cmd)` | `ST7735S_WriteCommand`: CS (PB5) low, D/C = command via `sub_01E5714E(1, 0)`, send 1 byte, CS high |
| `sub_01E3E4A6(byte)` | `ST7735S_WriteData`: same with D/C = data (`sub_01E5714E(1, 1)`) |
| `sub_01E3E508(byte)` | write one data byte (used for window coordinates) |
| `sub_01E3E430(buf, len)` | SPI transmit buffer |
| `sub_01E3E3B8(hz)` | set SPI clock (always 20 000 000) |
| `sub_01E3E518(y, h)` | set window: CASET 32..159, RASET y..y+h-1 |
| `sub_01E3E53C()` | RAMWR (0x2C) |
| `sub_01E3E540(...)` | set window and push pixels |
| `sub_01E3F58A()` | clear screen: window rows 0..127, write 0x8000 bytes (128×128×2) |
| `sub_01E3D150(ms)` | millisecond delay |

Pins: **CS = PB5** (GPIO 21), **RESET = PB4** (GPIO 20), **backlight = PB7** (active high, `sub_01E41CA8`),
**D/C = PR1** (power-domain port, set by `sub_01E5714E` through the mask-ROM P33 routines),
**SCK = PB0, MOSI = PB1** (SPI1 at 0x1E1D00, port A; the stock's SPI base table at 0x01E64A90 is
SPI0/1/2 = 0x1E1C00/0x1E1D00/0x1E1E00, and the BR23 SDK's pin table gives SPI1 port A = PB0/PB1/PB2).

Visible area is 128×128. With MADCTL 0xA8 (MY | MV | BGR) the panel's columns are offset by 32
(CASET 32..159).

Init sequence (from board init `sub_01E485EC` at 0x01E48A10):

```c
// reset: PB4 high, 1 ms, low, 1 ms, high; then 60 ms
ST7735S_Cmd(0x11);                                   // SLPOUT  (then delay)
ST7735S_Cmd(0xB1); Data(0x05, 0x3A, 0x3A);           // FRMCTR1
ST7735S_Cmd(0xB2); Data(0x05, 0x3A, 0x3A);           // FRMCTR2
ST7735S_Cmd(0xB3); Data(0x05, 0x3A, 0x3A, 0x05, 0x3A, 0x3A); // FRMCTR3
ST7735S_Cmd(0xB4); Data(0x03);                       // INVCTR
ST7735S_Cmd(0xC0); Data(0x62, 0x02, 0x04);           // PWCTR1
ST7735S_Cmd(0xC1); Data(0xC0);                       // PWCTR2
ST7735S_Cmd(0xC2); Data(0x0D, 0x00);                 // PWCTR3
ST7735S_Cmd(0xC3); Data(0x8D, 0x6A);                 // PWCTR4
ST7735S_Cmd(0xC4); Data(0x8D, 0xEE);                 // PWCTR5
ST7735S_Cmd(0xC5); Data(0x12);                       // VMCTR1
ST7735S_Cmd(0xE0); Data(0x03,0x1B,0x12,0x11,0x3F,0x3A,0x32,0x34,0x2F,0x2B,0x30,0x3A,0x00,0x01,0x02,0x05); // GMCTRP1
ST7735S_Cmd(0xE1); Data(0x03,0x1B,0x12,0x11,0x32,0x2F,0x2A,0x2F,0x2E,0x2C,0x35,0x3F,0x00,0x00,0x01,0x05); // GMCTRN1
ST7735S_Cmd(0xFC); Data(0x8C);                       // PWCTR6 (ST7735S only)
ST7735S_Cmd(0x36); Data(0xA8);                       // MADCTL: MY | MV | BGR (landscape)
ST7735S_Cmd(0x3A); Data(0x05);                       // COLMOD: 16-bit RGB565
// sub_01E3E11E(128), clear screen (sub_01E3F58A)
ST7735S_Cmd(0x29);                                   // DISPON
```

## Keypad: 4×4 matrix plus 4 direct keys

`sub_01E46C8C` is `KEYBOARD_Poll()`, the same scheme as the UV-K5's `keyboard.c`:

- **Rows** (outputs, active low): PA6, PA7, PA8, PA9. `sub_01E46C30(mask)` sets port A bits,
  `sub_01E46BD4(mask)` clears them (mask bits 6..9).
- **Columns** (inputs with pull-ups): PC2, PC3, PC4, PC5, read from SFR `0x1E5084` (port C IN), mask `0x3C`.
- Before scanning, all rows are driven low; if no column is low, it returns `0xFF` (no key).
- Then for scan step 0..4 it writes `set` mask then `clear` mask, waits, and reads the columns:

| Step | set (PA) | clear (PA) | Row driven low |
| --- | --- | --- | --- |
| 0 | 0x3C0 | 0x000 | none (keys wired straight to GND) |
| 1 | 0x380 | 0x040 | PA6 |
| 2 | 0x340 | 0x080 | PA7 |
| 3 | 0x2C0 | 0x100 | PA8 |
| 4 | 0x1C0 | 0x200 | PA9 |

Key codes (`u32` table at 0x01E65E94, 16 bytes per step; column 0 = PC2 … 3 = PC5):

| Step | PC2 | PC3 | PC4 | PC5 |
| --- | --- | --- | --- | --- |
| 0 (direct) | 0 | 3 | 4 | 2 |
| 1 (PA6) | 8 | 7 | 6 | 5 |
| 2 (PA7) | 20 | 19 | 18 | 17 |
| 3 (PA8) | 16 | 15 | 14 | 13 |
| 4 (PA9) | 12 | 11 | 10 | 9 |

### Key translation (`sub_01E4F1AA`, the 10 ms key/UI task)

The scan code indexes two byte tables at 0x01E65A98 (`id`, the digit value) and 0x01E65AAD (`event`),
and a 32-bit mask `0x000AEE00` marks the codes whose `id + '0'` is entered as a digit.
Press/hold/release state lives at RAM 0x198C0 (`r5`): debounce counter at +0, long press at 100 ticks,
repeat from 150 ticks (flags 16 = press, 32 = repeat, 64 = long, 128 = release).

| Code | Matrix position | id | event | Key (confidence) |
| --- | --- | --- | --- | --- |
| 9 | PA9 × PC5 | 1 | 0x10 | **1** (certain) |
| 10 | PA9 × PC4 | 2 | 0x10 | **2** (certain) |
| 11 | PA9 × PC3 | 3 | 0x10 | **3** (certain) |
| 13 | PA8 × PC5 | 4 | 0x10 | **4** (certain) |
| 14 | PA8 × PC4 | 5 | 0x10 | **5** (certain) |
| 15 | PA8 × PC3 | 6 | 0x10 | **6** (certain) |
| 17 | PA7 × PC5 | 7 | 0x10 | **7** (certain) |
| 18 | PA7 × PC4 | 8 | 0x67 | **8** (certain; has its own event, a second function) |
| 19 | PA7 × PC3 | 9 | 0x10 | **9** (certain) |
| 5 | PA6 × PC5 | 0 | 0x11 | `*` (inferred from the grid) |
| 6 | PA6 × PC4 | 0 | 0x13 | **0** (inferred from the grid) |
| 7 | PA6 × PC3 | 0 | 0x15 | `#` (inferred from the grid) |
| 12 | PA9 × PC2 | 0 | 0x17 | 4th-column key, row 1 (unknown) |
| 16 | PA8 × PC2 | 0 | 0x22 | 4th-column key, row 2 (unknown) |
| 20 | PA7 × PC2 | 0 | 0x18 | 4th-column key, row 3 (unknown) |
| 8 | PA6 × PC2 | 0 | 0x12 | 4th-column key, row 4 (unknown) |
| 0 | direct, PC2 | 0 | 0x50 | direct key (unknown; held with PA4 at power-on enters a special boot mode) |
| 3 | direct, PC3 | 0 | 0x1E | direct key (unknown) |
| 4 | direct, PC4 | 0 | 0x08 | direct key (unknown) |
| 2 | direct, PC5 | 0 | 0x1F | direct key (unknown) |

The digits form a 3×3 grid (columns PC5/PC4/PC3, rows PA9/PA8/PA7) with the PA6 row below it, so
`*`/`0`/`#` on PA6 follows the usual phone layout. The eight unknowns are MENU, EXIT, UP, DOWN and the
other function keys; pressing each on a radio running a key-test build will settle them.

### PTT, side keys and power

| Pin | Role | Evidence |
| --- | --- | --- |
| **PA4** | PTT (main / VFO A) | `sub_01E4D5BC` (PTT handler) starts TX when PA4 or PA12 is low; PA4 + direct key 0 at power-on enters a special mode (`sub_01E492C0`) |
| **PA12** | second PTT (VFO B) | same handler; flag RAM 0x198C0+2849 = 1 when PA12 is the one pressed, then it switches the active VFO (0x198C0+5878) to match before keying |
| **PB11** | power-off sense | `sub_01E4C978`: high for 25 ms (bit 11 of the IN register set) → save state, drop PB10, wait 250 ms, halt |
| **PB10** | power hold latch (high = stay on) | driven high in board init, low on power-off |
| PB2 | pulled-up input, no reader found yet | |

Which of PA4/PA12 is the physical PTT and which is a side key needs a check on hardware.

### TX/RX switching

`sub_01E3FC64(mode)` writes BK4819 REG_30 (egzumer's values):

| mode | REG_30 | Meaning |
| --- | --- | --- |
| 0 | 0x0000 | off |
| 1 | 0xBFF1 | RX on (`BK4819_RX_TurnOn`) |
| 2 | 0xC1FE | TX on (`BK4819_PrepareTransmit`) |
| 3 | 0x0302 | (unknown, short mode) |
| 4 | 0xC3FA | TX for tones/DTMF (assumed) |

The key task enters TX at 0x01E5051E: REG_51 = 0, REG_7D tweak, then mode 2.

## GPIO setup (board init `sub_01E485EC`)

Generated with `python tools/re/gpio_init.py build/app.dis sub_01E485EC`:

| GPIO | Pin | Dir | Pull-up | Pull-down | Initial out |
| --- | --- | --- | --- | --- | --- |
| 1 | PA1 | out | - | - | 1 |
| 3 | PA3 | out | 0 | 0 | 1 |
| 4 | PA4 | in | 1 | 0 | - |
| 6 | PA6 | out | 0 | 0 | - |
| 7 | PA7 | out | 0 | 0 | - |
| 8 | PA8 | out | 0 | 0 | - |
| 9 | PA9 | out | 0 | 0 | - |
| 10 | PA10 | out | 0 | 0 | - |
| 12 | PA12 | in | 1 | 0 | - |
| 16 | PB0 | out | 0 | 0 | 0 |
| 17 | PB1 | out | 0 | 0 | 0 |
| 18 | PB2 | in | 1 | 0 | - |
| 20 | PB4 | out | 0 | 0 | 0 |
| 21 | PB5 | out | 0 | 0 | 1 |
| 22 | PB6 | out | 0 | 0 | - |
| 23 | PB7 | out | 0 | 0 | - |
| 24 | PB8 | out | 0 | 0 | - |
| 25 | PB9 | out | 0 | 0 | - |
| 26 | PB10 | out | 0 | 0 | 1 |
| 27 | PB11 | in | 1 | 0 | - |
| 32 | PC0 | out | 1 | 0 | - |
| 33 | PC1 | out | 0 | 0 | 1 |
| 34 | PC2 | in | 1 | 0 | - |
| 35 | PC3 | in | 1 | 0 | - |
| 36 | PC4 | in | 1 | 0 | - |
| 37 | PC5 | in | 1 | 0 | - |
| 39 | PC7 | out | 0 | 0 | - |

Known roles so far: PA4 PTT, PA12 second PTT, PA6–PA9 keypad rows, PC2–PC5 keypad columns,
PA10/PC0/PC1 BK4819 SCK/SDATA/SCN, PB4 LCD reset, PB5 LCD CS, PB10 power hold, PB11 power-off sense.
The rest are in the next section.

## Peripheral sweep of the stock code (2026-11-21)

Every GPIO and peripheral the stock app touches, from a static scan of `build/app.dis`. Made with three small
scripts in `tools/re/`: `periph_scan.py` (pins and SFR bases per function), `pin_uses.py` (what each pin set-up
does to the port registers) and `callers.py` (every call with its constant arguments). None of this has been tried
on a radio yet: each row says how sure it is.

### Pin map, the rest of the pins

| Pin | Role | How sure | Evidence |
| --- | --- | --- | --- |
| **PB8** | speaker amplifier enable (high = on) | strong | `sub_01E400D6(n)` keeps six "wants audio" flags at RAM `0x19137`–`0x1913C` (odd `n` sets flag `(n-1)/2`, even `n` clears it, 0 clears all) and drives PB8 high while any is set. The tone player (`sub_01E4A67A`, `res/tone/B4`) sets PB8 directly; power-off (`sub_01E4C89C`) drops it |
| **PB6** | flashlight (LAMP) | strong | `sub_01E41906(0/1/2)` = off/on/toggle, state at `0x19136`. The key task toggles it when the side-key setting (`0x198C0+2831`) is 2, the third entry of the list `FM RADIO, VOX, LAMP, SOS` |
| **PB9** | green RX LED | likely | `sub_01E40CE0(0/1/2)` = off/on/toggle, state at `0x19135`. Switched on with `BK4819_SetAF(on)` and amp flag 2 when the squelch opens (`sub_01E4E408`), off on every return to RX and on TX |
| **PR0** | red TX LED | likely | `sub_01E418E0(0/1/2)` sets PR0 through P33 (`sub_01E5714E(0, x)`), state at `0x19134`. Switched on right after the TX checks pass (`0x01E5061A`) |
| **PC7** | receive front-end select (with BK4819 GPIO5) | strong for the switching, the names are guesses | `sub_01E41382(band, mode)`, below |
| **PA1** | the mic level for VOX, ADC channel 0 | strong | stock `0x01E50060` reads channel 0 (>> 2) and compares it with the VOX thresholds `21, 31, … 111` (`0x01E65721`, by VOX level; 8 lower while transmitting), then holds transmit for (VOX delay + 5) × 100 ms. `sub_01E4698A` (board init) clears DIE, pulls and output on PA1 and adds channel 0 to the ADC scan list (`sub_01E46964`, list at RAM `0x1A1D4`, 10 entries) |
| **PB3** | battery, ADC channel 6 | strong | same function, channel 6. Stock `sub_01E46A30` reads channel 6 for the battery: the 10-bit result >> 2, shown as value × 435 / 10000 V and compared with the six thresholds at external flash `0xF200`. The ADC is at `0x1E3100` (+0 CON, +4 RES): CON = `0xF06E` \| channel << 8, then bits 4 and 6 set; bit 7 = result ready, bit 6 again starts the next (stock `sub_01E3CD74`, `sub_01E3CE94`) |
| **PA3** | external flash chip select | known | the external-flash routines; see "External SPI flash" |
| PB2 | pulled-up input | unknown | set in board init; no reader found (maybe the earphone or charge detect) |
| PA0, PA2, PA5, PA11, PA15, PD5 | low-power settings only | – | touched only by the sleep and power-off pin set-ups (`sub_01E48246`, `sub_01E48318`, `sub_01E483DC`, `sub_01E69ECC`, `sub_01E4751E`) |

The three LED/lamp helpers have the same shape: 0 = off, 1 = on, anything else = toggle. In SOS/alarm mode
(`sub_01E52FAE` at `0x01E53B48`) the stock code flips PB9, PB6 and PR0 once a second, PB9 and PB6 together and PR0
the other way.

### Band switching: BK4819 GPIOs plus PC7

`sub_01E4135C(mask, on)` sets BK4819 GPIO pins through `REG_33` (read it, clear `mask << 8` to make the pin an
output, set or clear `mask` in the low byte). The band comes from the frequency (`sub_01E3F682`/`sub_01E3F6AA`,
frequency in 10 Hz units) into RAM `0x198C0+5918`:

| Band | Range | RX: PC7 / GPIO5 (0x20) | TX: GPIO3 (0x08) / GPIO4 (0x10) |
| --- | --- | --- | --- |
| 0 | below 200 MHz | high / 0 | 1 / 0 |
| 4 | 108.000–135.999 MHz (airband) | high / 0 | 0 / 0 (no TX path) |
| 2 | 200–300 MHz | high / 0 | 0 / 0 |
| 3 | 300–400 MHz | low / 1 | 0 / 1 |
| 1 | 400 MHz and up | low / 1 | 0 / 1 |

`mode` is 0 = idle (GPIO3 = GPIO4 = 0, PC7 high, GPIO5 = 1), 1 = RX, 2 = TX. So GPIO3 is the VHF transmit path and
GPIO4 the UHF one; PC7 and GPIO5 pick the receive front end. Bands 2 and 4 get no transmit path at all.

### Calibration: in the external flash, not at `0x93000`

### Settings and VFO logs (external flash)

The stock app keeps its settings and the two VFOs as logs, one 4 KB sector each (checked on one radio's dump: all
slots pass their CRC):

| Log | Header (slot bitmap) | Slots | Slot | Wrap |
| --- | --- | --- | --- | --- |
| VFO A and B | `0x8000`, 8 bytes | from `0x8010`, 0x42 bytes: A (32), B (32), CRC-16/XMODEM of the 64 | written after the current one while it is ≤ 59 | erase `0x8000`, write slot 0 (`0x01E40836`) |
| settings | `0x9000`, 6 bytes | from `0x9010`, 0x60 bytes: 64 setting bytes, 30 zero, CRC of the first 0x5E | while ≤ 40 | erase `0x9000`, write slot 0 (`sub_01E40620`) |

The header is one bit a slot, bit 0 of byte 0 first; the first bit still set is the current slot (`sub_01E40386`).
A save writes the next slot, then clears the current slot's bit (byte `index / 8` written with `FE FC F8 F0 E0 C0
80 00`[index % 8], table `0x01E5C06D`). The settings save is skipped when the slot would be the same. Programs are
split at 256-byte pages (`sub_01E40526`); erase is `06`, `20` + address, then status `05` until bit 0 clears
(`sub_01E40586`). VFO record byte 18 is 4 on every airband record and 0 on all others in one radio's log: not the
band index; not traced.

The calibration is a plain (unencrypted) table at **external flash `0xF000`–`0xF27F`**, read row by row at
power-on by `sub_01E48E22` (16-byte rows into RAM `0x6AF8` and on; read with `sub_01E3EAEE(addr, buf, len)`).
Codeplug Studio has a field-by-field map (its `docs/reference/radios/baofeng/uv-5r-mini/calibration.md`, traced on
V0.04); the V0.27 code agrees where checked here:

| Where | What | V0.27 evidence |
| --- | --- | --- |
| `0xF000` / `0xF080` / `0xF040` | PA bias frames for power index 0 (High), 1 (Low), 2; each four 16-byte rows: +0x00 UHF ((MHz − 400) / 10), +0x10 VHF ((MHz − 130) / 3, at most 15), +0x20 200 MHz band ((MHz − 200) / 5), +0x30 350 MHz band ((MHz − 350) / 5, zero = no TX) | row pointer tables at `0x01E6574C` (by band, then power index) and the index sums at `0x01E50522`–`0x01E505F2` |
| | `REG_36` = bias << 8 \| gain; gain `0xFF` / `0xD7` / `0xFF` by power index; bias 0 = `0x007F` (PA off) | `sub_01E4DA78`, table `0x01E5DF02` |
| `0xF0C0` / `0xF0D0` | squelch open / close thresholds, levels 0–9, written to `REG_78` minus a per-frequency offset | (Codeplug Studio) |
| `0xF0E0`, `0xF0F0`, `0xF100`, `0xF110` | squelch offsets: UHF, VHF, 200 MHz, 350 MHz | (Codeplug Studio) |
| `0xF200` | battery level thresholds, 6 bytes | (Codeplug Studio) |
| `0xF210`–`0xF216` | mic sensitivity, sub-tone levels, AF gain, frequency trim index | (Codeplug Studio) |
| `0xF230` | BCD band limits for the programming software; not used by the radio | `sub_01E4CCDA` reads it for the clone reply |

The menu offers "High" and "Low" only (strings at `0x01E5C1EC`). Read with our own code (`app/calib.c`) from a dump of
one radio: High 0xD7 / Low 0x96 at 145 MHz, High 0xD2 / Low 0x96 at 446 MHz, nothing at 350 MHz; squelch 3 opens at
an RSSI count of 108 at 145 MHz (about −106 dBm if the count is in the RSSI register's 0.5 dB steps). It is unique
to each radio: our code never writes the external flash (`driver/extflash.c` has no write or erase code).

Internal flash `0x93000` (the sector before `VM`) is 4 KB of high-entropy data, not this table, and the app
never reads it directly. It may be JieLi's own factory data. `VM` itself holds only a few short JieLi records
(`0x94000`–`0x94174` on this radio). Keep both untouched (AGENTS.md rule 6).

### Audio: the MCU's DAC, FM radio inside the MCU

- **Audio DAC: used.** The voice prompts and tones (`res/tone/*.wtg`, `*.sin` in the internal flash at
  `0x7C000`) are decoded by JieLi's audio code and played through the AC695N's own DAC. Audio set-up:
  `sub_01E48CB4` (reads DAC trims from `VM` items 88 and 92). Registers: digital volume at `0x1E2F20`/`0x1E2F24`
  (four 16-bit channels, `sub_01E37C72`), analog gain 5 bits per channel at `0x1E19C4` (`sub_01E37C08`), DAC
  enable bit 31 of `0x1E19C8` (`sub_01E37CDA`). So the audio block is at `0x1E2F00` and its analog side at
  `0x1E19C0`.
- **Receive audio** goes from the BK4819 to the speaker without the MCU: stock only unmutes the BK4819
  (`BK4819_SetAF`), asks for the amp (PB8) and lights PB9. The LM2904 dual op-amp is probably where the BK4819
  audio and the MCU's DAC are mixed before the amp (not traced on the board).
- **MCU audio input (mic/line-in ADC): only in the Bluetooth call path.** Radio receive audio is never fed into
  the MCU and back out of the DAC: no line-in pass-through is started (the `adc_linein` and `mic_stream` tasks
  sit in the task table at `0x01E6432C`/`0x01E6435C`, but no code names them). The one place that opens an audio
  input is JieLi's Bluetooth hands-free handler (`sub_01E49B56`): an 8 kHz CVSD or 16 kHz mSBC encoder for a phone
  call over Bluetooth, with echo cancelling (`audio_enc`, `aec`). Whether the radio ever enters that mode, and what
  (if anything) is wired to the MCU's mic pin, is open. VOX, tones and DTMF are all done by the BK4819.
- **FM broadcast radio: the AC695N's built-in FM receiver** (`fm_inside`, `sub_01E4B3D0`/`sub_01E4B49A`/`sub_01E4B688`),
  87.5–108.0 MHz in 100 kHz steps (frequency at `0x198C0+104`, set by `sub_01E3DE8E`), heard through the DAC.
  It is a driver in a one-entry table at `0x01E00674` (name at +20; functions: `0x01E3D184` open, `0x01E3DD50`,
  `0x01E3DE4C`, `0x01E3DE80`, `0x01E3DE8A`). Not a tuner chip: the open function sets the band (8750–10800, step
  100), allocates about 10 KB (6040 + 36 + 4096 + 8 bytes), fills a table of fixed-point filter coefficients, gates a
  clock (`0x1E0010` +52 bit 24, CON bits 22–23 = 1) and installs interrupt 43 (handler `0x02003D8A`, in the RAM
  code). The front end is a block at `0x1F1700` (+0 CON, bits 0 and 3 on; +8, +12 bit 31), tuning goes through RAM
  code (`0x0200382C`, `0x02003950`), and the sound goes out through the audio DAC (`0x1E2F00`). So the demodulation is
  partly in software on the MCU: an FM mode needs a front-end driver, the DSP and a DAC driver, none of which exists in
  our code yet, and none of which can be checked on the PC (the emulator has no FM block).

### Clocks, timer tick, watchdog

| What | Found |
| --- | --- |
| Crystal | 24 MHz (`sub_01E69C62` starts the PLL from 24,000,000) |
| System clock | `clk_set("sys", hz)` = `sub_01E69AAC`, capped at 240 MHz. Stock picks the speed from JieLi's clock-manager table at `0x01E670B0` (24 MHz idle, more for decoders and FM); `sub_01E43174` sets 24 MHz |
| UART clock | 48 MHz (`sub_01E4317C`, matches the 9600 baud seen on the cable) |
| Tick | timer at `0x1E0500`, interrupt 5, handler `0x01E568C4` (clears pending with CON `|= 0x4000`, counts in RAM `0x199E4`); set up by `sub_01E46946` |
| Watchdog | P33 `0x80` (`WDT_CON`). Early in `main` stock clears its enable bit (`p33_and(0x80, 0xEF)`), writes 0 and then 12 (`sub_01E46924`); clear = `p33_or(0x80, 0x40)`. 12 is 4 s in JieLi's usual table (not checked) |

## NRF (V0.27) and NRFB (V0.04)

Both are `Hardware:V1.7`. Found by unpacking both stock files (`tools/setup.py`), lifting both
(`tools/lift/lift.py --variant nrfb`) and matching functions with addresses blanked out (`tools/variant_diff.py`).

| | NRF V0.27 | NRFB V0.04 |
| --- | --- | --- |
| `.ufw` size, SHA-256 | 504,832, `811b4b37…9128` | 520,192, `02419efb…f74e` |
| `app.bin` length | 0x77AC8 | 0x77D1C (+596 bytes) |
| `PRCT` (protected, includes calibration) | @0x1000 len 0x93000 | @0x1000 len 0x94000 |
| `VM` (settings) | @0x94000 len 0x6A000 | @0x95000 len 0x68000 |
| `ota.bin` (loader) | blank, 0x200 | a loader, 0x3D27 |
| `uboot.boot`, `isd_config.ini` | identical | identical |
| Code functions (lifted) | 2,032 | 1,974 |

- **The same code, apart from the radio app.** 1,803 NRF functions are identical in NRFB, with only addresses
  different: all of the JieLi SDK (Bluetooth, audio, RTOS, USB, flash). About 168 changed and about 61 small NRF
  functions are gone, which looks like inlining by a different compiler setting. The changes cluster in
  0x01E4C000–0x01E52000, the radio app. Among the named ones: `APP_PowerOnInit` (also the `V0.27`/`V0.04` text),
  `BOARD_Init`, `APP_TimeSlice10ms`, `KEYBOARD_Poll`, `BK4819_TxRxSwitch`, `BK4819_SetAF`,
  `BK4819_GetCxCSSScanResult`, `BOOT_CheckSpecialMode`, `UI_FramebufferPut`.
- **The same board, as far as the code shows.** `BOARD_Init` has the same instructions; only RAM offsets differ by
  1–4 bytes. The keypad and BK4819 pins are the same.
- **One BK4819 setting differs.** `BK4819_TxRxSwitch` writes 0x1D0F on NRF and 0x9D1F on NRFB (two more bits set).
  `KEYBOARD_Poll` gains a constant 0x309.
- **Never flash one kind's file on the other.** The update refuses it (MM9PDY), and the settings and the end of the
  protected area sit 4 KB apart: a cross-flashed radio could lose its settings or its calibration.
- **Version numbers are two separate lines**, not one sequence: there's no evidence which is newer, and neither
  image holds a build date.

## How MM9PDY's patches make room (1.1.0, NRF)

[MM9PDY's mod](https://github.com/pskillen/mm9pdy-firmware) patches the stock app. Applying its NRF 1.1.0 patch to
the stock file and comparing the two apps showed:

- **The app keeps its size** (0x77AC8), and 67 places change, about 13 KB in all.
- **Most of the new code, about 11.6 KB, overwrites a stock image** at 0x01E5E21E. It's RGB565 data, mostly the
  pixel 0x1082, part of one block that runs to 0x01E633C0 and is referenced from the code at 0x01E5441E. The rest
  of the changes are short hooks into the stock code.
- **Their own loader `uart_user.bin` is appended** to the `.ufw` (520,704 bytes after patching).

So the stock app has no free space: a patch-based mod must replace something, such as image data.

## Room in flash

- The stock app (~479 KB) fills its space; the bytes after it up to the settings area at 0x94000 look unused (about
  80–100 KB on NRF). This is worked out from the layout only; check it before putting anything there.
- The settings area is ~424 KB, much more than channels need; the update also stores its loader there.
- A from-scratch firmware without the JieLi audio SDK should be far smaller than stock. The UV-K5's open firmware
  is about 60 KB. JieLi's full Bluetooth would take an estimated 150–250 KB of it ([BLUETOOTH.md](BLUETOOTH.md)).

## The board (from photos)

Board photos by **briand**: [briand/uv5r_mini_hardware](https://github.com/briand/uv5r_mini_hardware) (CC0). Thank
you! What the photos show, set against what the firmware told us:

| On the board | Marking | Matches the firmware? |
| --- | --- | --- |
| MCU, QFP-48, with a PCB-trace Bluetooth antenna near the bottom edge | sanded off | yes: the `.ufw` names the chip `AC695N`, which has built-in Bluetooth |
| Radio chip, QFN-32, with its own crystal | `FD6818 CS4144XA` | the firmware drives it exactly as a BK4819, so it's probably a rebranded or compatible part. Keep an eye out for registers that behave differently from BK4819 notes |
| SOP-8 next to the radio chip | `516A-UGL` | the external SPI flash (CS PA3): Giantec **GT25Q16A**, 16 Mbit (2 MB), identified by SourceDiver42 in [issue #2](https://github.com/briand/uv5r_mini_hardware/issues/2) |
| Square colour LCD | – | yes: ST7735S, 128×128 |
| Two-pin K-plug jacks | – | the cable; the firmware routes the cable UART to the USB pins (D+ 61, D− 62) |
| Clear LED at the top | – | the flashlight |
| Dual op-amp | `3PEAK LM2904B` | not seen in the firmware (analogue: audio or sensing) |
| MCU crystal | `R24 BfC2` | possibly 24 MHz; not checked |
| Speaker | `16Ω 1W` | – |

- **Board label: `5R_MINI V1.6 2025/06/09`.** Radio Info's `Hardware: V1.7` is a fixed string in the app, not read
  from the board, so the version on screen doesn't identify the board.
- **Pads `V G M P` near the bottom edge** (top side, beside the Bluetooth antenna): the chip's USB port. Measured,
  see below.

### Measured with a multimeter (V1.6 board, battery out)

Measured by MegaTheLEGEND on 2026-11-20. Chip pin numbers are from the AC6951C datasheet's LQFP48 7×7 pinout,
which the board matches: pin 23 = USBDM, pin 24 = USBDP. SourceDiver42's traces in
[issue #3](https://github.com/briand/uv5r_mini_hardware/issues/3) agree: pin 24 to `P`, pin 12 (BT_RF) to the
antenna, pins 14–15 (BT_OSCI/OSCO) to the crystal.

| From | To | Result | Means |
| --- | --- | --- | --- |
| `P` | MCU pin 24 | continuity | `P` = USB D+ |
| `M` | MCU pin 23 | continuity | `M` = USB D− |
| `G` | battery minus | continuity | `G` = ground |
| `M` | `P` | open | separate lines |
| `M` | 3.5 mm jack sleeve | 99.5 Ω | the cable reaches D− through a ~100 Ω resistor |
| `P` | every jack contact | open | the cable doesn't reach D+ |
| Battery USB-C port | – | charge only, no data lines | no USB route there |

`V` was not traced; it is probably VBUS. What this means:

- The cable uses one wire (D−, pin 62 in the firmware's GPIO numbering) for the update-mode UART. How RX gets
  in on the same wire, or elsewhere, is still to work out.
- A USB connection to the chip (for JieLi's ROM download mode) needs wires on `G`, `M` and `P`, with the case open.
  Leave `V` unconnected: the battery powers the chip.
- How to enter the ROM download mode on this radio is still unknown.

### ROM USB download mode: works (read and write)

Done on 2026-11-21 on an NRF V0.27 radio, case open, with [tools/jl_usbkey](../tools/jl_usbkey/README.md) (an
ESP32 sending JieLi's USB_KEY) and kagaimiq's [jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool):

- The key was answered 2.7 s after pressing the ESP32's button, and the PC showed `BR23 UBOOT1.00`. BR23 is
  JieLi's family name for the AC695N.
- The tool's loader runs from RAM. Chip key **`0xB50F`**, the same key the stock `.ufw` is encrypted with. Flash ID
  `0x856014`: 1 MB SPI NOR on SPI0.
- Two reads of the whole 1 MB were identical. On Windows the tool needs an administrator shell.
- Flash `0x1000`–`0x7C000` is byte for byte the stock file's `flash.bin`, so the dump is clean and the radio
  runs unmodified V0.27. `0x0000`–`0x1000` is blank.
- What the stock `.ufw` doesn't have (an update writes only up to `0x7C000`):

  | Where | What |
  | --- | --- |
  | `0x7C000`–`0x93000` | Sound resources: a directory of about 186 JieLi `.wtg` (compressed voice) and `.sin` (tone) files, named like `C32.wtg` and `E0.wtg` (probably the Chinese and English voice prompts). Encrypted with the app's key stream (chip key, block key counted from `0x4000`). Written at the factory |
  | `0x93000` | One sector of high-entropy data, unique to each radio. Not the radio calibration after all: that is in the external flash at `0xF000` (peripheral sweep) |
  | `0x94000` | Start of `VM` (settings); the rest of `VM` was blank |
  | `0xFE000` | `BTIF` (Bluetooth info) |

  So no stock file holds a radio's calibration or its voice prompts: a backup from the radio itself is the only copy.
- Switching the radio off and on afterwards starts it normally.

**Writing works too** (2026-11-21): on one blank 4 KB sector (`0xF1000`, in the empty part of `VM`), the loader
erased it, wrote a test pattern that read back the same, and erased it again; the whole flash then read back
byte for byte equal to the backup. A **full restore** worked as well: all 256 sectors erased and rewritten from the backup, each checked, and
the whole flash then equal to it byte for byte (jl-uboot-tool's loader commands, one sector at a time). The radio
started normally afterwards. One
earlier try lost the USB link on the first read, before anything was written; the radio was on a USB hub. The backup stays on the owner's PC (`firmware/backups/`, git-ignored): it
holds Baofeng's code and the radio's own calibration.

### External SPI flash, read (2026-11-21)

Read in the same ROM download mode with [tools/jl_extflash](../tools/jl_extflash/README.md): a 350-byte RAM
program of ours bit-bangs the flash (CS PA3, SCK PB0, MOSI PB1, MISO PB2). JEDEC ID **`C46015`**: Giantec, 2 MB.
Two reads identical. The contents are **not encrypted**.

| Flash | What |
| --- | --- |
| `0x000000`–`0x009000` | channel memory: 32-byte records, the frequency first in BCD (`50622546` = 462.5625 MHz), names such as `FRS 1` and `GMRS 1` |
| `0x009000`–`0x010000` | settings |
| `0x00F000`–`0x00F280` | per-radio calibration (inside the settings range; see "Calibration" in the peripheral sweep) |
| `0x010000`–`0x08C000` | font and icon bitmaps |
| `0x0C4000`–`0x0D0000` | RGB565 picture data |
| `0x100000`–`0x200000` | blank |

The top megabyte is blank, so the 1 MB dump in briand's issue #2 likely has everything after all.

Every pin the firmware uses exists on the LQFP48 package: keypad PA6–PA9 and PC2–PC5, BK4819 PA10, PC0, PC1,
LCD PB4 and PB5 (PB5 = pin 48, LCD chip select), power PB10 and PB11, PTT-B PA12.

### From the issues on briand's repo

- **MCU:** [issue #3](https://github.com/briand/uv5r_mini_hardware/issues/3) suggests an Artery AT32WB415 (ARM),
  as in the UV-5RM and UV-5RH. The firmware rules that out for these radios. The `.ufw` names the chip `AC695N`,
  the app is pi32v2 code (not ARM), our open pi32v2 build rebuilds the stock image byte for byte, and the emulator
  runs it as pi32v2. The MCU is a JieLi AC695N.
- **External flash dump:** [issue #2](https://github.com/briand/uv5r_mini_hardware/issues/2) has a dump of the SPI
  flash, read with the programmer set to A25L80P (8 Mbit). It holds channel memory. The chip is 16 Mbit, so the
  dump is only its first half, but our full read shows the top half is blank, so it likely holds everything.
- **Update mode:** [issue #4](https://github.com/briand/uv5r_mini_hardware/issues/4): A and `=` at power-on are
  enough. That's PTT-A + `=`, as the firmware checks.

## Tones sent while transmitting

| What | Stock | Registers |
| --- | --- | --- |
| DTMF generator on | `sub_01E4D40A(1)` | `REG_24 = 0x8A7F`, `REG_70 = 0xE0E0`, `REG_3F = 0x0800` |
| DTMF pair | `sub_01E4D44C(w1, w2)` | `REG_71 = w1`, `REG_72 = w2`; table of 16 pairs at `0x01E66136` (0-9, A-D, *, #), word = Hz x 10.32444 rounded, then 850 Hz and 1000 Hz |
| One tone on | `sub_01E40B24(1)` | `REG_70 = (REG_70 \| 0xBC00) & 0xFCFF`, then TX mode 4 |
| One tone frequency | `sub_01E40B52(f)` | `REG_71 = f x 1032444 / 10000`, f in 10 Hz; codes 17-20 = 100, 145, 175, 210 (bytes at `0x01E654A2`) |
| Off | `0x01E5072C` | `REG_71/72 = 0`, `REG_24` bit 5 cleared, `REG_70 = 0`, back to TX mode 2, AF mute, amp source 4 off |
| Heard locally | `sub_01E510A2`, setting `0x09` bit 0 | TX mode 4 (`REG_30 = 0xC3FA`, the mic out), `SetAF(241)` = `REG_47 = 0x6140`, `REG_48 = 0xB8A2` |
| Roger beep | `sub_01E4DCC2`, setting `0x17` | the above with 1000 Hz then 850 Hz, 40 ms each |
| Tail elimination | `sub_01E4D7BA(1)` from `sub_01E4DCC2`, setting `0x14` | no tone or CTCSS: `REG_51 = (level \| 0x9000) & 0x907F`, `REG_07 = 1137` (55 Hz), `REG_52` bits 13-15 cleared; DCS: `REG_52` bit 15 set. Off: `REG_51 = 0` |
| Transmit tail | `sub_01E4DCC2` sets a 16-bit count at `0x198C0+2797` to 40; the 10 ms tick (`0x01E55044`, inside the 2 ms timer's divide-by-5) counts it down and the main loop (`0x01E4FB9E`) keeps transmitting until 0 | 400 ms after PTT is let go, with or without the settings above |

`sub_01E510A2(code)` is called only from the key handler during transmit (`0x01E5457E`): digits give their own code,
four other keys give 12-15. `sub_01E400D6(n)` is the amp vote: odd n = a source on, even = off (9/10 = tones).

### PTT-ID (stock `sub_01E4DABA`, sent by `0x01E531D8`)

| Where | What |
| --- | --- |
| external `0xA000`, 11 bytes (read at boot by `sub_01E46F3E` into RAM `0x198C0+559`) | 0-4 own ID codes (0xFF ends), 5 unknown, 6 PTT-ID (bit 0 up code, bit 1 down code; 0xFF on a radio seen), 7 code time, 8 gap time (both 0-5, 6+ read as 1), 9 separator (0-5 = A-D, *, #; 6+ as 4), 10 group call code (0-6 = none, A-D, *, #; 7+ as 5) |
| external `0xA020`, 20 x 16 bytes | group call list: 3 codes, 2 x 0xFF, name in GBK (a radio's own: "成员1"...) |
| external `0xA180` / `0xA190`, 16 bytes each | up code (sent at TX start) and down code (at PTT release), codes 0-15, 0xFF ends |
| channel byte 12 / 13 | signal code (group call entry) / PTT-ID 0-3; the channel bit and the `0xA000` byte 6 bit are ANDed (`0x01E507B0`, `0x01E508C2`) |
| setting `0x0C` | wait before the up code: (n x 50 + 50) counts |
| setting `0x09` | DTMF sidetone: bit 0 keys, bit 1 PTT-ID (0 off, 1 DT, 2 ANI, 3 both) |

Timing: one counter at `0x198C0+5214` (`h[2607]`), decremented on every call of the stock timer routine
(`0x01E55246`, outside its divide-by-5 10 ms part), taken to be 2 ms: wait 100 ms + 100 ms x n, code and gap
{5, 10, 20, 30, 40, 50} x 5 counts (table `0x01E6556E`) = 50-500 ms. Each code: the pair for the code time, then
`(0, 0)` for the gap time; after the last code (16 at most or 0xFF) a 10 ms wait and everything off. The monitor
(TX mode 4, AF 241, amp source 4) is switched on once at the start when setting `0x09` bit 1 is set.

### Transmit end and the repeater tail

- At PTT release (`sub_01E4DCC2`): roger, tail tone, PTT-ID down code, then a 400 ms count; if setting `0x16` is not
  0, `sub_01E419B6` loads the count at `0x198C0+2803` with setting `0x16` + 2.
- When the 400 ms run out (`0x01E4FF68`): BK4819 mode 0, band switch off, then the count at `0x198C0+2803` = setting
  `0x15`. While it isn't 0 the receive audio isn't switched on (`sub_01E4E2C0`, `0x01E50864`). Codeplug Studio counts
  it in 100 ms (decrement at `0x01E55140`).
- VFO mode takes its PTT-ID from setting `0x0B` (0 off, 1 BOT, 2 EOT, 3 both); channel mode from channel byte 13
  (`sub_01E3F9D0`, into `0x198C0+5905`).

