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
