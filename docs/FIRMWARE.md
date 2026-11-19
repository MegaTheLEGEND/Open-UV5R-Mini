# Stock firmware format (`stock-nrf-v0.27.ufw`)

## Cipher

JieLi "ENC": each byte is XORed with the low byte of a CRC16-CCITT LFSR, which then steps
`key = (key << 1) ^ (0x1021 if key & 0x8000 else 0)`.

## Layers

1. **`.ufw` container** (key `0xFFFF` per record)
   - 0x00: 32-byte header, chip id `AC695N`
   - 0x40, 0x90: 0x50-byte file records: `flash.bin` @0x200 len 0x7B000, `ota.bin` @0x7B200 len 0x200
2. **`flash.bin`**: JieLi flash image; header and 32-byte entries at 0x20 (key `0xFFFF`)
   - `uboot.boot` @0xA0 len 0x1FE8
   - `isd_config.ini` @0x2088 len 0x71
   - `app_dir_head` @0x3000 (the app area)
   - `key_mac` @0xFF000 len 0x1000
3. **App area** (from flash 0x3000): each 32-byte block encrypted with
   `key = 0xB50F ^ (((addr - 0x3000) >> 2) & 0xFFFF)` (chip key `0xB50F`)
   - `app.bin` @0x100 len 0x77AC8, **runs at `0x01E00100`** (flash mapped at 0x01E00000)
   - `VM` @0x94000 len 0x6A000 (settings storage)
   - `PRCT` @0x1000 len 0x93000, `BTIF` @0xFE000 len 0x1000

## app.bin

- Startup at 0x01E00100: `call`, set `sp`/`ssp` = 0xD910, copy .data from 0x01E77BA0 to RAM
  0x800000 region, zero .bss, clear `icfg` bits 8–9, call init, jump to `main`.
- 95.4% of bytes decode as pi32v2 by linear sweep (the rest is text/tables).
- Contains the JieLi soundbox SDK (Bluetooth HFP/A2DP, FM, USB audio) plus the radio application:
  menus `VFO Mode`, `RX CTCSS`, `RX DCS`, `Squelch`, `Scan Mode`, `PTT-ID`, `DTMF-ST`,
  `Wireless Cps`, … and the string `%s NRF`.
