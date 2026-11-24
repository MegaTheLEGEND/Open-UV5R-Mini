# Repo layout, names and assets

How the repo is arranged, what to call things, and where pictures and other assets go. Follow it for new code.

## Folders

Everything that runs on the radio is under `src/`. Everything else is a tool, a test or a document.

| Path | What goes there |
| --- | --- |
| `src/` | The radio firmware, and nothing else. Start-up: `main.c`, `board.c` / `board.h` (pins), `start.S`, `firmware.ld` |
| `src/bsp/` | Chip registers and addresses (AC695N): plain definitions, no code |
| `src/driver/` | One driver per chip or peripheral: `bk4819`, `st7735s`, `keyboard`, `uart`, `gpio`, `system`, `adc`, `flash`, `extflash`, `io`. Drivers know registers and pins, never the radio's behaviour |
| `src/radio/` | Radio behaviour with no screen: the radio core (`radio.c`: tune, squelch, TX), tones, scan, dual watch, VOX, DTMF sequences |
| `src/store/` | Data kept in the radio's flash, read the stock way: calibration, channels, settings |
| `src/app/` | The application: `app.c` (keys, screens, the main loop), `keys.h`, the key test, battery; the apps (`apps.h`, one file each, each behind an `ENABLE_APP_*` switch) |
| `src/ui/` | Drawing: font, text and icon helpers, colours (`theme.h`), one file per screen (`screen_main.c`), and the generated `icons.c` / `icons.h` |
| `src/update/` | The cable update mode. Safety-critical (AGENTS.md rule 3): change it only with `tests/update_host.py` and `tests/flasher_guard.py` passing |
| `src/bt/`, `src/platform/` | Bluetooth interface (stub by default) and the JieLi SDK glue for the Bluetooth edition |
| `assets/` | Source pictures for the firmware (below). Turned into C by `tools/mkassets.py` |
| `tools/` | PC tools: unpack, build, check and flash images, the disassembler, the lifter (`tools/lift/`), the asset generator |
| `emu/` | The emulator that runs the stock app |
| `tests/` | PC tests (`*_host.py`, `ui_preview.py`, `flasher_guard.py`) and small programs for a radio (`tests/radio/`) |
| `docs/` | Documents |
| `build/`, `firmware/` | Local only, git-ignored: build output, the stock files and your radio's backups. Never commit them |

Dependencies go one way: `app` → `ui`, `radio`, `store` → `driver` → `bsp`. A driver never includes `radio/` or
`app/`; `ui/` never reads the radio (the app hands it a snapshot), which is why screens can be drawn on the PC.

Includes are written from `src/`: `#include "radio/radio.h"`, `#include "driver/bk4819.h"`.

## Names

| Thing | Style | Example |
| --- | --- | --- |
| Folders and files | lower case, `_` between words, a `.c` / `.h` pair per module | `src/radio/dualwatch.c`, `src/ui/screen_main.c` |
| Screens | `src/ui/screen_<name>.c` | `screen_main.c`, later `screen_menu.c` |
| Public functions | `MODULE_Verb` or `MODULE_VerbNoun`, the module in capitals | `RADIO_Apply`, `BK4819_SetAF`, `UI_DrawIcon` |
| Static functions | lower case with `_` | `side_view`, `draw_meter` |
| Types | `Name_t`, CamelCase | `Vfo_t`, `UiMain_t`, `Icon_t` |
| Enum values and macros | upper case, with the module's prefix | `RADIO_TX`, `UI_STATE_RX`, `FREQ_MIN` |
| Colours | `COLOR_<ROLE>` in `src/ui/theme.h`, named by what they mean, not their hue | `COLOR_RX`, `COLOR_WARN` |
| Globals | `gName`; file statics `s_name` | `gVfo`, `s_active` |
| Stock references | in comments only, by address | "stock `sub_01E4DE3C`" |
| Frequencies | `uint32_t` in 10 Hz units, as the BK4819 | `14552000u` = 145.520 MHz |

Comments say what the hardware needs and where the fact comes from. Say what was tested and how (on the PC, in the
emulator, on a radio).

## Assets

Pictures the firmware draws are kept as text, so they can be edited in any editor and their changes read in a
diff. They are turned into C by a tool; the C is committed so the firmware builds without Python.

### Icons

One file per icon: `assets/icons/<group>_<name>.txt`.

```
// "FM" tag
.###########.
#.....#.###.#
#.#####..#..#
...
```

- `#` is a lit pixel, `.` is off. Every row the same width, at most 32 × 32.
- Lines starting with `//` say what the icon shows; they end up as a comment in `src/ui/icons.h`.
- Icons are 1 bit. The code picks the colours when it draws them (`UI_DrawIcon(&ICON_MOD_FM, x, y, fg, bg)`), so one
  file serves every state (active, dimmed, warning). Colours come from `src/ui/theme.h`.
- Tags (labels like `FM`, `DCS`) are lit boxes with the letters cut out of them, 9 rows tall, with the 5×7 font's
  letters. Status icons are 9 × 9 or smaller, so they fit the 11-pixel status bar.
- Things that show a level (battery cells, S-meter, power bars) are drawn by code from a value, not one icon per
  level.

Groups in use:

| Group | For | Examples |
| --- | --- | --- |
| `status_` | the status bar | `status_battery`, `status_scan`, `status_lamp`, `status_warning`, `status_dualwatch` |
| `side_` | the A / B badges | `side_a`, `side_b` |
| `state_` | receiving or transmitting | `state_rx`, `state_tx` |
| `mod_`, `bw_`, `tone_`, `shift_` | a side's settings, as tags | `mod_fm`, `bw_narrow`, `tone_dcs`, `shift_plus` |

The C name is `ICON_` and the file name in capitals: `assets/icons/status_lamp.txt` → `ICON_STATUS_LAMP`.

### Making and checking

```powershell
python tools/mkassets.py            # after changing anything in assets/: rewrites src/ui/icons.c and icons.h
python tests/ui_preview.py          # draws the main screen on the PC into build/ui/*.png to look at
```

`make check` runs `tools/mkassets.py --check` and fails if the generated files are out of date. Never edit
`src/ui/icons.c` or `icons.h` by hand.

### Later

Other asset kinds follow the same pattern: a text or image source under `assets/<kind>/`, a generator in `tools/`,
generated C under `src/`. Likely next: the font (`assets/fonts/`), and voice prompts if they are ever played
(those would be our own recordings: the stock prompts are Baofeng's and stay out of the repo).
