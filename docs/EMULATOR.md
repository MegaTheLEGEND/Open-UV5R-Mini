# Emulator (work in progress)

`emu/uv5r_emu.py` runs the radio's own `app.bin` on an emulated AC695N, aiming to show the radio's screen and take
key presses on a PC, so firmware changes can be tried before they go near a radio.

There are two emulators in this project; don't confuse them:

| | What it emulates | State |
| --- | --- | --- |
| `tools/uv5r_flasher.py` built-in radio | The **update protocol and the flash memory** (no CPU) | Done: flash, power cut, resume, boot check |
| `emu/uv5r_emu.py` (this page) | The **CPU and hardware**, running the real firmware | Boots part way, paused here |

```powershell
python emu/uv5r_emu.py --isa E:/pi32v2-isa --steps 8000000
```

It needs `build/stock/unpacked/app.bin` (made by `tools/build_matching.py`) and a pi32v2-isa checkout. It prints
how far it got, the peripheral registers touched, the ROM calls, the external-flash pages read, and writes the
emulated LCD to `build/emu-screen.png`. Options: `--watch LO-HI` prints writes to SFR addresses with the PC,
`--trace N` prints the last N PCs, `--extflash FILE` loads a dump of the external flash, `--no-stuck` keeps going
through busy-wait delays (our build's `SYSTEM_DelayUs`), and `--app build/fw/firmware.bin` runs our own build.

Our build in the emulator (2026-11-23): the LCD's D/C follows our own P33 writes (PR_OUT, 0x8099) as well as the
stock routine, and the flash answers its JEDEC ID (`9F` → `C4 60 15`). With that, our SPI1 driver passes its
self-test and draws the update-mode screen by DMA. The key inputs read as pressed, so our build always takes the
PTT-A + `=` branch into the update mode there.

## Where it is (2026-11-19, paused)

About **8 million instructions** into boot, at ~85,000 instructions/s (Python). In that time the app:

1. runs the start-up code: sets `sp`/`ssp`, copies 53 KB of code to RAM 0, clears `.bss`, enters `main` (0x01E47546);
2. sets up clocks and power through the P33 power-domain routines, and calibrates a clock with timer 0;
3. creates its RTOS objects and the first task (`app_core`, from the task table at 0x01E64260), starts the scheduler,
   and carries on into `APP_PowerOnInit` (0x01E46F3E);
4. reads the **external SPI flash** (command `03`, address `0x00A000` and on) and starts LCD traffic: a window
   (`2A`/`2B`) and a pixel write (`2C`). It stops in `SPI1_Wait` (0x01E3E3DC) spinning until its time-outs end.

No real picture yet: the first frame is still blank.

## How it is built

- **CPU**: pi32v2-isa's simulator (`tools/llvm/sim.py`, MIT-0), whose instruction effects are models fitted to
  real hardware runs. Used as a library from the pi32v2-isa checkout, not copied. This file adds the special
  registers it leaves to its host (`sp`, `ssp`, `usp`, `icfg`, `reti`… via `li.sr_u32`, `mov.sr_r`, `mov.r_sr`).
- **Memory map**: RAM at 0 (256 KB), second RAM window at 0x800000, flash cache window at 0x01E00000 (the
  decrypted app area, `app.bin` at +0x100), peripheral registers at 0x1E0000–0x1FFFFF and 0x100000–0x10FFFF,
  area at 0x02000000. Correction (2026-11-21): that area is not a mask ROM but the code the stock app copies
  into RAM at start-up (flash 0x01E6AAAC -> RAM 0, 53492 bytes), run through the 0x02000000 alias; see RE-NOTES.
- **Mask ROM**: not available, so each routine the app calls is replaced by a Python stand-in. The app calls
  about 130 ROM entry points (list: `grep -oE "; 0x020[0-9A-F]{5}" build/app.dis | sort | uniq -c`).
  Unknown ones are logged once and return 0.

### ROM routines identified

| Address | Name | How it was identified |
| --- | --- | --- |
| 0x021127A4–0x021127F0 | `memcpy`, `memset`, `strcmp`, `strlen`, … | JieLi br23 SDK `cpu/br23/maskrom_stubs.ld`, at ROM base + its address |
| 0x02000058 / 0x020000A0 | `p33_rx(addr)` / `p33_tx(addr, val)` | LCD D/C on PR1 is set this way (`P33_SetPrBit`) |
| 0x02000156 / 0x0200084A | `p33_or(addr, mask)` / `p33_and(addr, mask)` | call sites: `p33_or(0x80, 0x40)` is the watchdog clear |
| 0x020012BE / 0x020012DA | `irq_save` / `irq_restore` | bracket every shared-state update (274 / 255 calls) |
| 0x02002824 | semaphore create `(sem, count)` | followed by posts and pends on the same object |
| 0x0200288E | semaphore post `(sem)` | |
| 0x020027D6 | semaphore pend `(sem, timeout)` | |
| 0x020028F0 | task queue pend | result compared with 21 in a loop |
| 0x020020BE | `os_time_dly(ticks)` | small constant arguments in polling loops |
| 0x02002B0E | task create `(func, arg, prio, stack, qsize, name)` | from the task table at 0x01E64260 |
| 0x02002DB8 | scheduler start | main carries on after it |
| 0x02000040 | short delay (guess) | 32, 96, 2 between register writes |

Only the libc, P33, `irq_*` and the delay are implemented; the RTOS ones are logged and return 0.

### Peripherals modelled

| Block | Address | Model |
| --- | --- | --- |
| Timers | 0x1E0400 + 0x10·n | CON bit 0–1 mode, bit 14 clears pending, bit 15 pending. A started timer is pending at once (enough for the clock calibration) |
| Hardware lock | 0x101044 | each write of 0xE7 flips bit 0 |
| GPIO | PA 0x1E5000, PB 0x1E5040, PC 0x1E5080, PD 0x1E50C0 (OUT at +0) | register file; PA3 and PB5 pick the SPI device |
| SPI1 | 0x1E1D00: CON +0, BAUD +4, BUF +8, DMA ADR +12, DMA CNT +16 | byte and DMA transfers, both directions, routed by chip select |
| ST7735S LCD | SPI1, CS = PB5, D/C = PR1 | `2A`/`2B`/`2C`/`36`, 132×162 RGB565 RAM, PNG screenshot |
| External SPI flash | SPI1, CS = PA3 | read commands `03`/`0B`; contents unknown (reads 0xFF) unless `--extflash` |

Everything else in the SFR space is a plain register file that reads back what was written.

## Findings that matter beyond the emulator

- **The RTOS is in the mask ROM.** The app's task switching, semaphores, queues and delays are ROM calls, not app
  code. The emulator has to stand in for all of it, and so would a fully open rebuild of the firmware.
- **There is a second flash chip.** An SPI NOR flash shares SPI1 with the LCD (CS = PA3). The app reads it from
  address 0x00A000 at power-on, probably fonts, icons or the start-up picture. Its contents are **not** in the `.ufw`
  update, so they have to be dumped from a radio (an SOIC-8 clip and a CH341A programmer would do). Until then those
  reads return 0xFF, and text or pictures drawn from it will be blank.

  Areas read in the first 4 million instructions: 0x000000–0x008FFF (about 36 KB, read in full; likely the
  channel and settings memory, which fits the clone addresses below 0xF000 that programming software uses),
  0x00A000, 0x00D000, and 0x0C0000–0x0C7FFF (32 KB read in 4 KiB pages; probably a font or picture). The
  boot was in the BK4819 code (return addresses 0x01E40C64, 0x01E40DFA on the stack) when the run ended.

## Recent changes (2026-11-19)

- `idle` (wait for an interrupt) now falls straight through: there are no interrupts yet. A 12-million-instruction
  boot reaches no new functions after about 2.3 million, so the code is probably waiting for an interrupt.
- `emu/uart_pins.py` and `emu/loader_store.py` call single stock routines with `Machine.call` to learn what they
  do (the cable pin setup; where the update loader is stored).
- `tools/lift/func_test.py` records every function call during a boot and checks the lifted C against it
  ([LIFTER.md](LIFTER.md)). Every new function the emulator reaches gets checked automatically, so more boot means
  more proven C.

## Road map (next steps, in order)

1. **SPI1 wait**: find why `SPI1_Wait` spins until its time-out after some transfers (the done flag must be set for
   every path that starts one), so boot runs at full speed.
2. **RTOS stand-in**: tasks as separate CPU contexts (registers, `sp`, `rets`, flags, if-block state), switched at
   blocking calls (semaphore pend, queue pend, delay) and on the tick. Semaphores, queues, delays and task create
   in Python. This is the largest piece of work.
3. **Interrupts**: the vector table the app installs in RAM, the tick timer, `rti`, and the 10 ms time slice
   (`APP_TimeSlice10ms`, 0x01E4F1AA).
4. **Board**: keypad (rows PA6–PA9, columns PC2–PC5), PTT (PA4, PA12), power-hold and power-off sense (PB10,
   PB11), a BK4819 register model (status, RSSI) so the radio screens settle.
5. **Window**: show the LCD live in a Tk window with the keypad, and put it in the flasher app as a "Run in
   emulator" button.
6. **External flash dump**, from a real radio, for complete screens.
7. **Speed**: profile first; then a faster core (the Python one manages ~85k instructions/s, slow next to the
   real chip).

Estimate: steps 1–4 to reach the main screen with working keys are several sessions of work. Step 6 needs a radio.
