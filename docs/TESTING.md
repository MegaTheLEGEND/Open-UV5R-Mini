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

## Steps

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

## Never flash the bare-metal C build

`build/fw/firmware.bin` (the key-test app) has no update mode. A radio running it could not enter `UPDATE...`, so it
could not be flashed again without opening the case. `tools/mkufw.py` refuses to put it in a flashable image unless
forced. It becomes testable once it implements the stock update hand-over, or a case-open recovery path is documented.
