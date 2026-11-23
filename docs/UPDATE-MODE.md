# Update mode in C

The cable update mode (PTT-A + `=` at power-on, `UPDATE...`) is the first stock feature ported to C, because a
radio running an app without it can never be flashed again.

| File | What |
| --- | --- |
| `src/update/update.c`, `src/update/update.h` | The update stage, hardware-independent: frames, rate exchange, container parsing, loader fetch and CRC checks, hand-over record |
| `src/update/update_radio.c` | On the radio: entry check, `UPDATE...` screen, the `UPDATE_Port` (UART, loader store, hand-over) |
| `src/driver/uart.c` | The cable UART at 0x1E2100, polled with DMA receive |
| `tests/update_host.py` | Runs `src/update/update.c` on the PC against the real flasher host |

## Status (2026-11-19)

**Protocol: done and tested on the PC.** `python tests/update_host.py` builds `src/update/update.c` with the host gcc and
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
   `src/driver/uart.c` (`route_usb_pins`) does the same.

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

`FLASH_ReadId` now does the same. With that, and SPI0 CON bit 3 set for one-line reads (as stock's status read
does; without it the answer read `FFFFFF`), the ID read on a radio: **`856014`** through **PA13**, a Puya P25Q80
(1 MB). PA13 is the flash chip select.

`src/driver/flash.c` now has read (`03`), 4 KB erase (`20`), page program (`02`), write enable (`06`) and status (`05`),
all in RAM code; erase and program refuse anything outside `0x1000`–`0x7C000`. On a radio (2026-11-21) the
key-test app's second PTT A press erased, programmed and read back `0x40000` (unused bytes past our app), then put
the original bytes back: **`WR OK`**, all 4096 bytes matched both times. Status register `00` (no block protection).
Our own erase and program work without any stock code.

**Update mode switched over (2026-11-21).** `src/update/update_radio.c` no longer calls any stock RAM code: flash reads,
erases and writes go through `src/driver/flash.c`, and the hand-over is our own:

| Step | Ours | Stock it follows |
| --- | --- | --- |
| Interrupts off, `0x10F1A8` = 6 | `port_handover` | `0x020012BE` |
| Record to `0x2FF88`, RAM protection opened | `port_handover` | as before |
| P33 register `0x32` = 8 if `0x1E511C` bit 5, else 4 | `port_handover` | `0x0200083A` |
| Stacks `0x10C20`/`0x11020`; `0x100300` bit 0 off; `0x1E0008` bits 8–10 = 3; flash controller off; `0x1E000C` \|= 3, wait, &= ~0x1C0; `0x1E0040` bit 0 off; `0x1E0008` = 0; registers 0; `rti` to `0x110000` | `src/update/handover.S` (RAM) | `0x020011E6` with `0x02000F18`, `0x02000F00`, `0x02000AF0` and the boot ROM's lock(0) |

`tests/update_host.py` passes against the real flasher host. On a radio: not yet. Open question: the UART baud
assumes a 48 MHz clock, and our start-up doesn't set clocks; `tools/uart_ping.py --scan` pings over the cable
(nothing else) and tells which rate the radio answers at.

**First cable test on a radio (2026-11-21).** Our app in update mode (PTT A + `=` at power-on), K1 cable (Prolific
PL2303) on the PC: `tools/uart_ping.py` got a clean answer at 9600 baud on the first ping, `aa 55 01 00 01 a5 82`.
So the UART reaches the USB pins as stock routes them, and its clock is 48 MHz without our own clock set-up. With the
K1 cable plugged in, the ROM USB rescue mode doesn't start (the plug's sleeve sits on D− through about 100 Ω):
unplug it first.

**The first real flasher run stalled** after the rate frame (nothing written; a read-back matched the image). Debug
frames from the radio (cmd 0x7F, `src/update/update_radio.c`) showed why: 105 bytes sat in the RX buffer (`HRXCNT = 0x69`)
with no time-out flag in CON0 (`0x141`), so the driver never handed them on, and later frames queued behind them
until another burst came. Stray `FF` bytes also arrive on the RX line. Fix: reload OTCNT on every re-arm and clear the
time-out flag with bit 7 then bit 10, as the stock ISR (`0x01E5949E`) does, plus a backstop: a byte count that hasn't
moved for 20 ms ends the burst. The update mode also acts on the power switch now (before, only taking the battery out
turned it off).

**Where it stands (end of 2026-11-21).** With that fix (`690b169`) the radio, in update mode, sent nothing at all:
no answers to pings and none of the once-a-second debug frames the build before had sent. Not yet understood; first
suspect is the new RX path (stall backstop, OTCNT reload) or the stray `FF` bytes, which may be the radio's own
transmissions coming back on the RX line. Nothing was written to the radio by any cable test (a read-back matched
the image). The radio was then put back on stock through the ROM USB rescue mode (`uv5r_restore.ps1`): all 256
sectors, equal to the backup byte for byte.

**Why it went silent, and the new receive path (2026-11-21, PC-tested only).** In JieLi's UART, `HRXCNT` holds a
count only after CON0 bit 7 (`RDC`) is written: the stock ISR writes bit 7, then bit 10, then reads `HRXCNT`; the
BR23 SDK's driver does the same. The `690b169` backstop read `HRXCNT` without it, so it acted on a stale count (the
bootloader leaves one behind) every 20 ms, re-arming the DMA again and again and handing on whatever was in the
buffer. `src/driver/uart.c` now:

| Step | How |
| --- | --- |
| End of a burst | time-out flag (bit 11) or buffer full (bit 14), or 20 ms without either (backstop) |
| Take the bytes | RDC, clear the flags, read `HRXCNT`, copy that many bytes into a FIFO at once, re-arm and reload OTCNT |
| Echo | bytes that arrive while we send are dropped (the protocol is half duplex; stray `FF`s were seen) |
| Waits | the TX wait has a time-out too |

The frame parser (`src/update/update.c`) also no longer drops a whole frame's worth of bytes after a bad CRC (it moves one
byte on, so a false `AA 55` can't swallow the real frame), and drops a part frame that goes quiet. Tested with
`python tests/uart_host.py`, a PC model of the UART (stale `HRXCNT`, missing time-out flag, 15 queued pings, a full
buffer, echo), and `tests/update_host.py` still passes. Not yet on a radio.

**Past the rate frame on a radio (2026-11-21, `d4a1a64`).** The flasher run with the `e9abb6b` receive path still
stalled after the rate frame: after each send the driver dropped everything received as echo, and the flasher's rate
frame, sent as soon as our ping answer arrived, fell in that window. Now only our own echo and leading `FF` noise are
dropped (`tests/uart_host.py` case 7). With that, `tools/uart_ping.py --rate` on a radio: ping answered, rate frame
taken, then the radio asked for the file header (cmd 2, offset 0, 64 bytes), four times with no answer, then only its
debug frames. Nothing was written.

**First full update from our firmware (2026-11-22, `5564993`, one radio).** Our app in update mode, the K1 cable, and
`tools/uv5r_flasher.py --loader-baud 9600` sending `build/stock-flashable-reference.ufw`, started by Sean. Our update
mode took the header, records and the whole loader (step and percent on the screen and in the debug frames), stored it,
and handed over. The stock loader then erased and wrote the app area, and the radio came up on stock V0.27. So our
update mode can always put stock back over the cable.

What it took on the way:

| Problem on a radio | Fix |
| --- | --- |
| A byte lost per reply: a 20 ms backstop re-armed the DMA mid-burst | collect on the time-out/full flag as stock does; a 1 s backstop only on a quiet line (`6ba978e`) |
| Reset back into our app right after the hand-over | watchdog off before the hand-over, as stock leaves it (`d3dcadf`) |
| Loader died when told to switch to 38400 | flasher option `--loader-baud 9600` keeps the loader at the rate already working |

**Both ways over the cable (2026-11-22, one radio).** With `--loader-baud 9600` and only the K1 cable, Sean installed
our build from stock (`build/open-uv5r-mini.ufw`, made with `make ufw BASE_UFW=build/stock-flashable-reference.ufw`
so it carries the loader) and put stock back from ours, twice each way. Slow (about 10 minutes) but it works.

Still open:

1. **38400 for the loader: a flasher and cable problem, not ours** (radio, 2026-11-22, Prolific K1 cable). Stock
   firmware failed at the switch exactly as ours did. Found and fixed in `tools/uv5r_flasher.py`, step by step:
   changing the port's rate while the rate frame was still leaving the adapter garbled it (now waits, and falls back to
   9600 if no hello comes; as Codeplug Studio's updater); then the loader took our short frames at 38400 but never a
   527-byte answer sent in one burst (four reads of 0x0, then 0x87, nothing erased; a 10 ms wait before answering
   didn't help). Sent in 32-byte pieces with 2 ms gaps (`--chunk-gap 2`) it got as far as erasing, then still lost
   too many answers and stopped part-way (0x87); power on with no keys (dark screen, the loader waits) and the flasher
   at 9600 continued and finished it: resuming works. Next: a larger gap. Until then `--loader-baud 9600`, about 10
   minutes. `--chunk-gap 10`: worse (four reads of 0x0, 0x87). `--loader-baud 19200`: further, then result 0x01
   ("not for this radio's key") with the stock image that passes at 9600, so data arrived wrong or out of step;
   nothing written. Even at 9600 the first reads are asked for two or three times. Suspect: this radio's rescue wires
   (a USB cable and an ESP32 through 220 Ω, soldered to the same D+/D− lines) load the line. **Confirmed:** with
   the wires unplugged from the ESP32, `--loader-baud 19200` worked (Sean, 2026-11-22). Disconnect the ESP32 for
   cable updates. Then `--loader-baud 115200` reached about 99% and the loader stopped (the chip held its own power:
   battery out); the resumed update would not switch to 115200 again, only 9600 worked. The flasher now finishes a
   resumed update at 9600 by itself. 57600 to try next. The stock hand-over also sets a 24 MHz system clock (`sub_01E43174`, `clk_set("sys")`, `sub_01E69AAC`;
   values from `emu/clk_trace.py`); ours doesn't, and trying it changed nothing at 38400.
   **Cause found (byte log, `build/flash-logs`, 2026-11-22):** the loader sends every read request twice, back to
   back, and the flasher answered both. The loader writes from the top down (0x7B000 to 0x3200, 512 bytes a read),
   so each block got a second 512-byte answer while it already waited for the next one; every block was re-asked
   (~0.36 s a block) and after the last one it gave up with **0x88**. The flasher now answers a request once (a copy
   within 0.1 s is skipped). That alone still ended in 0x88 (stock to ours, 2026-11-22): the loader re-asks a block
   after ~0.3 s, then sees our first answer and asks for the next block at once, so both requests arrive in one burst
   (167 times in one run). Answering both put the old block's data on the line while it waited for the new one, and
   it takes the next answer without checking its address: wrong data in a block, the area CRC fails, 0x88. The
   flasher now answers only the newest request. The emulated loader does all three (double requests, late answers,
   no address check): without the fix its flash ends with a bad CRC, with it the flash equals the image. Not yet
   tried on a radio.
2. Many retries: the flag collects only about a third of the replies, the 1 s backstop the rest.
3. If the loader resets mid-update, stock's app resumes it at power-on (the record at `0xFDF00`); ours doesn't yet,
   so the radio comes back in our app (seen when the 38400 switch failed). Update mode again recovers it.
4. The official Baofeng PC updater against our update mode: not tried. The `.ufw` as shipped has no loader, and ours
   refuses it (0x83); how the official tool supplies one is to find.

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
