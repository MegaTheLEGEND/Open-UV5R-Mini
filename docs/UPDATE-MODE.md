# Update mode in C

The cable update mode (PTT-A + `=` at power-on, `UPDATE...`) is the first stock feature ported to C, because a
radio running an app without it can never be flashed again.

| File | What |
| --- | --- |
| `app/update.c`, `app/update.h` | The update stage, hardware-independent: frames, rate exchange, container parsing, loader fetch and CRC checks, hand-over record |
| `app/update_radio.c` | On the radio: entry check, `UPDATE...` screen, the `UPDATE_Port` (UART, loader store, hand-over) |
| `driver/uart.c` | The cable UART at 0x1E2100, polled with DMA receive |
| `tests/update_host.py` | Runs `app/update.c` on the PC against the real flasher host |

## Status (2026-11-19)

**Protocol: done and tested on the PC.** `python tests/update_host.py` builds `app/update.c` with the host gcc and
runs it against `flash_radio()` from `tools/uv5r_flasher.py`, the same code that flashes real radios. After the
C code hands over, the flasher's emulated loader finishes the update. Checked:

- the C code answers the ping, agrees the rate, reads the header and records, finds `ota.bin`, and fetches the
  pinned loader byte for byte, checking the entry and data CRCs;
- the hand-over record is byte for byte the stock layout;
- the update ends with result 0, and the emulated flash equals the image and boots;
- a file without a loader (the stock `.ufw` as shipped) is refused with result 0x83, and nothing is handed over.

**On the radio: not flashable yet.** Two pieces are open, and `tools/mkufw.py` refuses to make a flashable image of
the C build until they are done:

1. **Storing the loader.** Done: `UPDATE_StoreLoader` writes the same bytes, at the same addresses, as the stock
   allocator `sub_01E42AF2` (learnt by running it in the emulator, `emu/loader_store.py`); `tests/update_host.py`
   checks it.
2. **Routing the UART to the USB pins.** Done: the stock entry receives on pin 61 (USB D+) and transmits on pin 62
   (USB D−). `emu/uart_pins.py` runs the stock pin setup in the emulator and prints every bit it sets or clears;
   `driver/uart.c` (`route_usb_pins`) does the same.

Then: test on the CPU emulator ([EMULATOR.md](EMULATOR.md)), then on a radio with a case-open recovery path at hand.

### Loader store: what is known (2026-11-19)

The store runs inside the closed library, at 0x01E691BE–0x01E693D8:

1. **Choose an address**: `sub_01E42AF2(…)` returns the destination in an out-parameter. It is **not a fixed
   address**. It reads two flash queries through `sub_01E42264` (ioctl 0x40045602, and 103, likely the flash
   size). It also reads the settings-store (VM) bank state at 0x198C0 + 4·{1523, 1526, 1677} and the bank
   selector at 0x198C0 + 6713. Areas are aligned to 128 KB (`& 0xFFFE0000`), and sizes are rounded to the
   flash's erase unit (`sub_01E42970`). When there isn't room, it **moves settings data in 512-byte chunks**
   (`sub_01E425EC` read, `sub_01E425DE` write) to make space. Result codes: 0xFE / 0xFF for no room
   (reported as 0x81 / 0x82).
2. **Erase and write**: `sub_01E429AE(addr, len)` erases (4 KB sectors, or 256-byte pages for short tails, via
   `sub_01E423A2(3, addr)`). The loader then goes in 512-byte chunks through `sub_01E425DE(addr, buf, len)`.
3. **Verify**: it reads everything back (`sub_01E425EC`) through the hardware CRC unit at 0x1E3500 and compares
   with the entry's data CRC. A mismatch gives result 0x83.
4. **Report**: the state callback runs with status 2 and `{loader_saddr = address − 32, priv, write_hdl =
   0x01E696C4}`. So `ota_addr` points at the 32-byte entry header, with the loader right after it. The app keeps
   these at 0x198C0 + 572 (`sub_01E43692`) and puts `ota_addr` in the record at +72.

Flash driver functions used: read `sub_01E421F6(buf, addr, len)`, write `sub_01E4256C`, erase command
`sub_01E423A2(cmd, addr)`, ioctl `sub_01E42264(cmd, arg)`.

So the address depends on the radio's current settings layout. It most likely falls in the VM area
(0x94000–0xFE000 on nrf-v0.27). That fits MM9PDY's note that their 1.1.0 loaders "put the radio's settings
back".

**Plan:** run the stock allocator itself in the CPU emulator to learn the rule, instead of deriving it by hand.
Replace the four flash-driver functions above with a Python flash model holding stock `flash.bin` plus a VM area.
Run the app's VM initialisation, then `sub_01E42AF2`. Try it with an empty store, a small one and a full one.
Port the rule to C only once the results agree with the disassembly.

## Our own flash driver and hand-over: what's needed (2026-11-21)

On a radio our app now runs (RE-NOTES, "First boot"), but the update mode can't work yet: the stock routines it
called at `0x0200xxxx` (flash read `0x0200048C`, program `0x02000772`, erase `0x020006BC`, `0x0200083A`, hand-over
`0x020011E6`) are the stock app's RAM code, which our app doesn't have. What they do, read from the stock RAM code
(flash `0x01E6AAAC` copied to RAM 0, run at `0x02000000`), to write our own:

| Step | Stock | What it does |
| --- | --- | --- |
| Lock | `0x020001A0` / `0x02000474` | interrupts off, then calls `0x021127D4` / `0x021127D8` with 1 (a real ROM above the RAM alias; probably pauses and resumes the flash controller's own use of the bus). To work out |
| Leave continuous read | `0x020004C8` | if the SFC (`0x1F0200`, bits 8–11) is in mode 6 or 7, send `FF FF` with CS low |
| CS | `0x020001CA` | GPIO: PD3 (`0x1E50C0` bit 3) or PA13 (`0x1E5000` bit 13), chosen by a flag at `0x198C0 + 1401` |
| Byte out | `0x02000226` | SPI0 `0x1E1C00`: CON \|= 0x8, CON &= ~0x1000, BUF (+8) = byte, wait CON bit 15, CON \|= 0x4000, CON &= ~0x8 |
| Byte in | `0x020002BE` | CON \|= 0x8 \| 0x1000, BUF = 0xFF, wait, read BUF |
| Write enable | `0x020005BE` | CS, `06`, CS off |
| Erase | `0x020006BC(cmd, addr)` | cmd 200 = 4 KB (`0x20`), 201 = 64 KB (`0xD8`), 204 = page (`0x81`); WREN, cmd + 24-bit address |
| Program | `0x02000772(buf, addr, len, dma)` | WREN, `0x02` + address + bytes |
| Cache | `0x020005D2` | drops the instruction-cache lines for the range (tags at `0xFC000`, 128 sets of 4) |
| Wait | `0x0200068E` | `05` until bit 0 clears |
| Back to continuous read | `0x02000512` | when it was left above |
| Hand-over | `0x020011E6` | stacks at `0x10C20`, three clean-up calls, all registers 0, `rti` to `0x110000` (the boot ROM) |

All of it must run from RAM, since the CPU can't fetch from the flash while it's being written. Plan: put our
routines in a section run at `0x02000000 + offset` and copied to RAM at start-up, as stock does. Test step by step on
a radio, with the USB rescue mode ready: first the flash's JEDEC ID (read only), then erase, write and read back a
blank settings sector, then the whole update.

**First flash test on a radio (2026-11-21)**, key-test app, `FLASH_ReadId` on PTT A:

| Register | Read | Meaning |
| --- | --- | --- |
| SFC CON `0x1F0200` | `002802B5` | mode 2 (not continuous read), on |
| SFC baud `0x1F0204` | `0003` | |
| SPI0 CON `0x1E1C00` | `00001028` before, `00009020` after | bit 0 is off: SPI0 not switched on |
| Port D OUT / DIR | `0018` / `FFEC` | PD3 is an input |
| PA13 OUT / DIR | 1 / 0 | output, high: most likely the flash chip select |

Both tries (PD3, PA13) timed out (`E2000000`); the app ran on afterwards, so pausing and restarting the flash
controller from RAM code works. The missing step is the stock lock: it writes 6 to `0x10F1A8` and calls the boot
ROM at `0x1127D4` (reached as `0x021127D4`) with 1, which most likely hands the flash pins to SPI0. The boot ROM (read
with a CPU copy in rescue mode; a plain read comes back scrambled) is about 10 KB at `0x110000`, with a jump table
at `0x1127C0`. Its two flash entries are short:

| Entry | Goes to | Does |
| --- | --- | --- |
| `0x1127D4` lock(1) | `0x112250` | wait `0x101000` bit 5 set, wait SFC CON bit 31 clear, SFC CON &= ~1, **SPI0 CON \|= 1** |
| `0x1127D8` unlock(1) | `0x11227C` | SPI0 CON &= ~1, SFC CON \|= 1 |

`FLASH_ReadId` now does the same, and tries PA13 first.

## How the stock app does it (v0.27)

**Entry** (`sub_01E492C0`): if PC2 (the `=` column) and PA4 (PTT-A) both read low at power-on:
1. draw `UPDATE...` (`sub_01E4917A`);
2. set up the UART at 9600 baud with DMA receive;
3. start the `uart_update` task (0x01E435DC).

**UART at 0x1E2100**:

| Offset | Register |
| --- | --- |
| +0 | CON0 (bit 15 TX done, 14 RX full, 11 time-out; write 13/12/10 to clear) |
| +4 | CON1 |
| +8 | BAUD = 48 MHz / baud / 4 − 1 |
| +16 | RX time-out |
| +20 / +24 | TX DMA address / count |
| +28 / +32 / +36 | RX DMA start / end / length (0x220) |
| +40 | bytes received |

The pins come from the table at 0x01E65538: `{0x3E, 0x3D, …}`.

**Protocol**: as JieLi's SDK `uart_update.c`, with the frames Codeplug Studio documents.
- Ping: host sends cmd 6; the app answers with an empty cmd 1.
- Rate: host sends cmd 1 {baud}. A different rate is set and echoed. The current rate starts the loader download.
- Reads: the app sends cmd 2 {offset, length}; the host answers with cmd 2 {offset, length, bytes}.

**Loader download** (closed library): reads the `.ufw` header and records, then `ota.bin`, then the
`uart_user.bin` entry:

| Offset | Field |
| --- | --- |
| +0 | entry CRC (bytes 2..31) |
| +2 | data CRC |
| +4 | offset |
| +8 | length |
| +12 | attribute 0x41 |
| +16 | name |

The loader is packed. Its own header gives an unpacked size of 18,112 and 0x9000, likely a load address.

**Hand-over** (`sub_01E42EE4`, `update_mode_api_v2`): build a 112-byte `UPDATA_PARM`:

| Offset | Field |
| --- | --- |
| +0 | CRC16 of bytes 2..79 |
| +2 | type 0x5A04 (UART) |
| +4 | result 0x5A01 (ready) |
| +6 | magic 0x5441 |
| +40 | pin 61 (USB D+, the UART's RX) |
| +44 | pin 62 (USB D−, the UART's TX) |
| +48 | baud |
| +52 | time-out 0 |
| +72 | `ota_addr` |

Copy it to RAM **0x2FF88**, close the interrupts and reset (`sub_01E435BA`, ending in ROM 0x020011E6). The
bootloader finds the record and starts the loader, which says hello at 9600 and takes over (docs/TESTING.md).
