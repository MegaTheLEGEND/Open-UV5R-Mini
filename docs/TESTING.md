# Testing on a real radio

**Flashing firmware can leave a radio unusable. You do it at your own risk.** Start with the matching build, which
is byte for byte the stock firmware.

Flashing facts below are from Codeplug Studio's UV-5R Mini flasher documentation
([pskillen/codeplug-studio](https://github.com/pskillen/codeplug-studio/blob/main/docs/reference/radios/baofeng/uv-5r-mini/firmware.md))
and the MM9PDY firmware project's guides
([pskillen/mm9pdy-firmware](https://github.com/pskillen/mm9pdy-firmware/tree/main/docs/baofeng-uv5rmini)),
whose flasher and to-stock image have been used on real radios. This project's own images have not been flashed yet.

## Which radios

Base **nrf-v0.27** only: Radio Info shows `Firmware:V0.27`, `Hardware:V1.7`, and holding **8** while switching on shows
`5RMINI NRF` for about a second. Radios on `NRFB V0.04` use a different flash layout: do not flash these images on them.

## How the update works

- **Update mode** is run by the stock app: switch off, hold **PTT-A** and **=**, switch on, let go when the computer
  picture and `UPDATE...` appear.
- **Cable:** the USB programming cable (CH340, FTDI FT232R or PL2303) in the K-plug.
- The radio drives the session: the stock app reads parts of the image at 9600 baud, then hands over to the
  **loader** (`uart_user.bin` in the image's `ota.bin`), which runs at 38400, erases 0x4000–0x7C000 and writes it.
- The stock `stock-nrf-v0.27.ufw` has **no loader** (its `ota.bin` is blank), so it can't be flashed as it is. Our
  flashable images carry the pinned L2 loader from MM9PDY's to-stock patch.

## Open flasher (this project's GUI)

```powershell
pip install pyserial
python tools/uv5r_flasher.py
```

Pick the stock `.ufw` and a firmware (a patch `.json` from `build_matching.py` or a flashable `.ufw`), then a radio:

- **Emulated radio (built in):** start here. It runs the whole update session against an emulated nrf-v0.27 radio
  with 1 MiB of emulated flash (starting as stock): app stage at 9600, loader hand-over at 38400, erase
  0x4000–0x7C000, write, result. Tick *Cut the power* to stop the write partway, then press **Flash** again: the
  emulated loader resumes from its record, like the real one. **Emu: boot check** decrypts the installed app and
  checks every CRC as the bootloader would, and shows the start-up string and version. It emulates the protocol and
  the flash, not the CPU: it does not run the firmware.
- **A COM port:** the USB programming cable. It asks you to confirm, then waits for the radio in update mode.

Before anything is sent, the image must pass every check: container, CRCs, a pinned nrf-v0.27 (L2) loader, area,
stock bytes outside the app window, and **the stock update mode present in the app**, so a radio can always be flashed
again. The flasher only ever sends image bytes the radio asked for, and echoes the radio's own frames.

Headless: `python tools/uv5r_flasher.py --cli --firmware build/open-uv5r-mini-nrf-v0.27.patch.json --port emu [--cut 0.4]`.

Tested against the emulator (2026-11-19): matching build, the demo build with a power cut at 40% and a resume (ending in
`OPENUV` on the boot check), and a refusal of an image without update mode. The serial path speaks the same protocol
but has **not been tried on a real radio yet**: for the first real flash, Codeplug Studio (below) is the proven tool.

## Steps (Codeplug Studio)

1. **Build:** `python tools/build_matching.py ...` (see [MATCHING.md](MATCHING.md)). It writes
   `build/open-uv5r-mini-<name>.patch.json` and stops if any check fails.
2. Charge the battery fully. Note your channels (or read them with CHIRP) as a precaution.
3. Open **Codeplug Studio** ([codeplug.mm9pdy.net](https://codeplug.mm9pdy.net)) in Chrome or Edge, go to the
   *Firmware flasher*, pick the **UV-5R Mini**.
4. Let it read the radio in normal mode (cable in, radio on).
5. Give it your `stock-nrf-v0.27.ufw` and our patch file **from disk**. Studio checks the patch result's SHA-256,
   the loader, the container CRCs and that only the app window differs from stock, and refuses anything else.
6. Put the radio in update mode (above) and press **Connect and flash**. About 3 minutes. Keep the cable in and the
   battery on until the radio restarts.

For the unmodified matching build (`nrf-v0.27`) the result is exactly MM9PDY's to-stock image, so Studio treats it
as **Back to stock**, and Studio's own Back to stock gives the same bytes.

## What to check after flashing

| Build | Check |
| --- | --- |
| `nrf-v0.27` (matching) | Radio Info `Firmware:V0.27`, `Hardware:V1.7`; hold-8 screen `5RMINI NRF`; channels, menus, scan, TX/RX as before |
| `open-uv5r-demo1` | as above, except the hold-8 screen shows `OPENUV NRF` |

## Recovery

- **Cut update:** leave the cable in, switch off and on with no keys held (the screen stays dark: the loader is
  waiting), then flash again with the same files. The loader continues where it stopped.
- **Back to stock:** flash the `nrf-v0.27` matching patch, or use Studio's *Back to stock* with MM9PDY's to-stock file.
- A power cut in the last moment of the write can need a case-open repair: keep the battery charged.

### If the app itself doesn't start (not verified)

- **The cable update mode is part of the app.** The app checks PTT-A + `=` at power-on and then hands over to the
  loader. An app that crashes before that check can't be updated with the cable. That is why a new app must run
  its update check first, before anything else.
- **A cut update is different:** the bootloader resumes it without the app (the dark screen above).
- **Last resort: the chip's ROM download mode.** JieLi chips have a factory programming mode in mask ROM that
  doesn't need the app. Community tools talk to it over USB, for example kagaimiq's `jl-uboot-tool`. On this radio,
  entering it probably needs the case open, to reach the USB data lines or a test pad. Not tried yet.
  The board has four pads labelled `V G M P` that look like USB (VBUS, GND, D−, D+): see
  [RE-NOTES.md](RE-NOTES.md#the-board-from-photos).
- **No flash clip:** the AC695N's flash is inside the chip package, so there's no flash chip to clip a programmer
  onto.
- MM9PDY's recovery page says some failures need a case-open repair, without saying how. Prove a recovery route on
  a radio before flashing any app of our own.

## Never flash the bare-metal C build

`build/fw/firmware.bin` (the key-test app) has no update mode. A radio running it could not enter `UPDATE...`, so it
could not be flashed again without opening the case. `tools/mkufw.py` refuses to put it in a flashable image unless
forced. It becomes testable once it implements the stock update hand-over, or a case-open recovery path is documented.
