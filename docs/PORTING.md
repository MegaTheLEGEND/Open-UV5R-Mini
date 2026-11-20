# C port status

Layout follows egzumer's UV-K5 firmware: `driver/` for chips, `ui/` for drawing, `board.*` for the pin map,
`main.c` for the application. Everything is written from the reverse-engineering notes; no stock or JieLi
code is used.

| Part | File | State |
| --- | --- | --- |
| Register map | `bsp/ac695n.h` | GPIO ports, SPI2 base, mask-ROM P33 accessors |
| GPIO | `driver/gpio.*` | done |
| Delays | `driver/system.*` | busy loops, calibrated for ≤96 MHz (clock not known yet) |
| BK4819 | `driver/bk4819.*` | bus, stock init sequence, RX/TX mode, frequency, RSSI, tone scan |
| ST7735S | `driver/st7735s.*` | stock init sequence; bit-banged on PB0/PB1 for now |
| Keypad, PTT | `driver/keyboard.*` | stock scan algorithm and key codes |
| Text | `ui/font.*`, `ui/helper.*` | 5×7 font, scalable |
| App | `main.c` | key-test screen: key code, PTT A/B, BK4819 REG_00, RSSI. Never transmits. |

## Build

```powershell
make TOOLCHAIN=C:/pi32v2-llvm/bin
```

Needs the open [pi32v2-llvm](https://github.com/jvitkauskas/pi32v2-llvm/releases) package only.
`make check` runs a host `gcc -fsyntax-only` pass without it.

## Open questions before the first flash

1. **Packaging.** `build/fw/firmware.bin` has to go back into a `.ufw` (re-encrypt with the chip key, fix the
   CRC16 fields in the headers). `tools/mkufw.py` is not written yet.
2. **Entry and clocks.** The bootloader jumps to 0x01E00100; it is assumed the PLL and flash cache are set up
   by then, as the stock app does no clock setup before its own init.
3. **Watchdog.** Not mapped. If the radio resets every few seconds, the watchdog needs clearing.
4. **LCD SPI pins** are confirmed: the stock uses SPI1 (0x1E1D00), and SPI1 port A is CLK = PB0,
   DO = PB1, DI = PB2. Moving from bit-banging to the SPI1 peripheral is a later speed-up.
5. **LCD D/C on PR1** goes through the mask-ROM P33 routines at 0x02000058/0x020000A0, as the stock does.
6. **Audio.** The speaker amplifier enable pin is not mapped, so RX audio is silent.
