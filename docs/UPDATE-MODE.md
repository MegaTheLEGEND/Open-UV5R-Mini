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

1. **Storing the loader.** The stock app does this inside JieLi's closed update library (0x01E686C4–0x01E69ECC).
   That code parses the container, picks out `uart_user.bin`, writes it to flash and reports its address as
   `ota_addr`. The address and the flash routine are not known yet. Until then `store_loader` refuses with
   result 0x86. The radio stays in `UPDATE...` and nothing is erased or written.
2. **Routing the UART to the USB pins.** The stock app routes UART TX to pin 61 (USB D+) and RX to pin 62 (USB D−)
   through `sub_01E001D2`…`sub_01E003B8` and the IO map (`sub_01E4856C`, `sub_01E484B6`). `driver/uart.c` sets up
   the UART itself from the stock register use, but not this routing.

Then: test on the CPU emulator ([EMULATOR.md](EMULATOR.md)), then on a radio with a case-open recovery path at hand.

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
| +40 | TX pin 61 |
| +44 | RX pin 62 |
| +48 | baud |
| +52 | time-out 0 |
| +72 | `ota_addr` |

Copy it to RAM **0x2FF88**, close the interrupts and reset (`sub_01E435BA`, ending in ROM 0x020011E6). The
bootloader finds the record and starts the loader, which says hello at 9600 and takes over (docs/TESTING.md).
