# C port status

> **Do not flash the bare-metal build.** It has no update mode, so a radio running it could not be flashed
> again without opening the case. Update mode is being ported first: [UPDATE-MODE.md](UPDATE-MODE.md) (protocol
> done and tested on the PC; loader store and USB-pin routing still open). Test on a radio with the matching build instead ([MATCHING.md](MATCHING.md),
> [TESTING.md](TESTING.md)); the C drivers will be moved into it function by function.

Layout follows egzumer's UV-K5 firmware: `driver/` for chips, `ui/` for drawing, `board.*` for the pin map,
`main.c` for the application. Everything is written from the reverse-engineering notes; no stock or JieLi
code is used.

| Part | File | State |
| --- | --- | --- |
| Register map | `bsp/ac695n.h` | GPIO ports, SPI1 base, mask-ROM P33 accessors |
| GPIO | `driver/gpio.*` | done |
| Delays | `driver/system.*` | busy loops, calibrated for ≤96 MHz (clock not known yet) |
| BK4819 | `driver/bk4819.*` | bus, stock init sequence, RX/TX mode, frequency, RSSI, tone scan |
| ST7735S | `driver/st7735s.*` | stock init sequence; bit-banged on PB0/PB1 for now |
| Keypad, PTT | `driver/keyboard.*` | stock scan algorithm and key codes |
| Text | `ui/font.*`, `ui/helper.*` | 5×7 font, scalable |
| App | `app/app.c` | key-test screen: key code, PTT A/B, BK4819 REG_00, RSSI, Bluetooth state. Never transmits. |
| Entry (open build) | `main.c`, `start.S` | bare-metal loop calling `APP_Tick()` every 10 ms |
| Bluetooth | `bt/`, `platform/jl_sdk/` | optional edition using JieLi's closed libraries from your own SDK; see [BLUETOOTH.md](BLUETOOTH.md) |

## Build

Tested 2026-11-19 with pi32v2-llvm `pi32v2-23.1.5-5` (Windows x64 release, SHA-256 checked) and MSYS2 `make`:

```powershell
make TOOLCHAIN=E:/toolchains/pi32v2-llvm/bin
```

Result: `build/fw/firmware.bin`, 3,624 bytes (3,620 code + 4 data), no warnings. The image starts at
0x01E00100 with `_start` (stacks, `.data` copy from flash to RAM 0, `.bss` clear, `main`), and the LCD D/C
code calls the mask-ROM P33 routines at 0x02000058/0x020000A0 the way the stock firmware does.
`make check` runs a host `gcc -fsyntax-only` pass without the pi32v2 toolchain.

## Open questions before the first flash

1. **Packaging** is done: `make ufw` puts `firmware.bin` into the app slot of your stock `.ufw`
   (`tools/mkufw.py`) and checks every CRC (`tools/ufwcheck.py`). Output: `build/open-uv5r-mini.ufw`.
   Not yet tried on a radio. Keep the stock `.ufw` to flash back.
2. **Entry and clocks.** The bootloader jumps to 0x01E00100; it is assumed the PLL and flash cache are set up
   by then, as the stock app does no clock setup before its own init.
3. **Watchdog.** Not mapped. If the radio resets every few seconds, the watchdog needs clearing.
4. **LCD SPI pins** are confirmed: the stock uses SPI1 (0x1E1D00), and SPI1 port A is CLK = PB0,
   DO = PB1, DI = PB2. Moving from bit-banging to the SPI1 peripheral is a later speed-up.
5. **LCD D/C on PR1** goes through the mask-ROM P33 routines at 0x02000058/0x020000A0, as the stock does.
6. **Audio.** The speaker amplifier enable pin is not mapped, so RX audio is silent.
