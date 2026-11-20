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
| **PB11** | power-off sense | `sub_01E4C978`: low for 25 ms → save state, drop PB10, wait 250 ms, halt |
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
