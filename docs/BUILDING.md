# Building the firmware

How to go from a fresh clone to a file you can put on the radio. Tested on Windows 11 in PowerShell (2026-11-22).
Only the NRF radio (stock V0.27) is supported; check yours first (README, "Which radio").

## 1. Install the tools (once)

| Tool | What for | Where |
| --- | --- | --- |
| Python 3.10+ | the build scripts and the flasher | python.org, or the Microsoft Store |
| `pip install pyserial` | the flasher talks to the cable | |
| git | getting the code | git-scm.com |
| MSYS2 | `make` and the small Unix tools it calls | msys2.org; then in the MSYS2 shell: `pacman -S make` |
| pi32v2-llvm | the compiler for the radio's CPU | [releases](https://github.com/jvitkauskas/pi32v2-llvm/releases) (Windows x64); unzip, e.g. to `E:\toolchains\pi32v2-llvm` |
| a host gcc (optional) | `make check` and the PC tests | MSYS2: `pacman -S mingw-w64-x86_64-gcc` |

## 2. Get the stock files (once)

Two files go in `firmware/` (git never stores them):

1. **The stock firmware**, `firmware/stock-nrf-v0.27.ufw`: the link is in README, "Which radio".
2. **MM9PDY's to-stock patch**, `firmware/uv5rmini-fw-to-stock-nrf-v0.27-hw1.7-1.1.0.patch.json`, from
   [pskillen/mm9pdy-firmware](https://raw.githubusercontent.com/pskillen/mm9pdy-firmware/main/releases/baofeng-uv5rmini/to-stock/uv5rmini-fw-to-stock-nrf-v0.27-hw1.7-1.1.0.patch.json).
   It adds the stock update loader, which the radio needs to install a file over the cable.

Then check and unpack the stock file:

```powershell
python tools/setup.py
```

It should say `NRF, firmware V0.27 ok`.

## 3. Open a terminal for building

In each new PowerShell window, add MSYS2's tools to the **end** of the path (at the front, its gcc would hide your
normal one and the PC tests would fail):

```powershell
$env:Path += ";C:\msys64\usr\bin"
```

## 4. Build

```powershell
make ufw TOOLCHAIN=E:/toolchains/pi32v2-llvm/bin
```

That's the one command you need. It compiles the firmware (`build/fw/firmware.bin`), makes the stock base with the
loader once (`build/stock-flashable-reference.ufw`), and writes **`build/open-uv5r-mini.ufw`**, then checks it. It
should end with `ALL OK`.

On Windows, `build.bat` in the repo folder does the same in one step (double-click it, or run it from a terminal;
extra arguments go to make, e.g. `build.bat ENABLE_CHIRP=0`). It expects MSYS2 at `C:\msys64` and the toolchain at
`E:/toolchains/pi32v2-llvm/bin`; set `MSYS_BIN` or `TOOLCHAIN` first if yours are elsewhere.

Other targets:

| Command | Result |
| --- | --- |
| `make TOOLCHAIN=...` | only `build/fw/firmware.bin` |
| `make ufw TOOLCHAIN=...` | the update file for the cable, `build/open-uv5r-mini.ufw` |
| `make flashimg TOOLCHAIN=... BACKUP=firmware/backups/internal-....bin` | the whole-flash image for the rescue route, `build/open-uv5r-mini-flash.bin`: your own backup with our app put in (only the app area changes) |
| `make check` | a quick syntax check with the host gcc; no pi32v2 compiler needed |
| `make clean` | deletes `build/fw` |

## 5. Choose features (optional)

`features.mk` switches features on (1) or off (0), as in egzumer's UV-K5 firmware. Edit it and build again;
everything that uses a changed switch is rebuilt.

| Switch | Default | What it does |
| --- | --- | --- |
| `ENABLE_CHIRP` | 1 | programming with CHIRP over the cable (UV-5R Mini driver, 115200) |
| `ENABLE_SAVE` | 1 | saving channel, VFO and settings changes; off: no flash-writing code at all |
| `ENABLE_TX` | 1 | transmitting, only in the bands allowed by default |
| `ENABLE_REGIONS` | 1 | region-specific transmit modes: the hidden REGION item (US, EU/UK, AU) and the TX LOCK modes that depend on it (GMRS, PMR446, UHF CB, each region's amateur band edges); 0 = nothing region specific, TX LOCK has only LOCKED, HAM (144–146, 430–440 MHz) and UNLOCKED |
| `REGION` | 0 | lock the image to one region: 0 = chosen in the menu; 1 US, 2 EU/UK, 3 AU (needs `ENABLE_REGIONS=1`) |
| `ENABLE_TX_UNLOCK` | 1 | the UNLOCKED TX LOCK mode; 0 = the image can't transmit outside the other modes' bands |
| `ENABLE_LCD_SPI` | 1 | screen and external flash through SPI1 hardware with DMA (checks itself at power-on, else bit-banged); 0 = always bit-banged |
| `ENABLE_SCREEN_TEST` | 0 | start-up check of the CHIRP cable set-up (labels on the screen) |

For one build without editing the file, put the switch on the command line:

```powershell
make ufw TOOLCHAIN=E:/toolchains/pi32v2-llvm/bin ENABLE_CHIRP=0
```

## 6. Put it on the radio

**With the cable** (the K1 programming cable; unplug the rescue ESP32 first if your radio has one):

```powershell
python tools/uv5r_flasher.py
```

In the window pick **From file**, choose `build/open-uv5r-mini.ufw`, pick the port, and press **Flash**. Then
switch the radio off, hold **PTT-A** and **=** and switch it on; let go at `UPDATE...`. With our firmware already
on the radio this is the same. **Stock** in the window puts the stock firmware back the same way. Loader speed
19200 is the safe choice (worked every time); 115200 should work since `34b1a8f` but is not yet confirmed. If an update stops part-way: take the battery out,
put it back, switch on with **no keys** (the screen stays dark), and press Flash again; it carries on at 9600.

Without the window: `python tools/uv5r_flasher.py --cli --port COM5 --firmware build/open-uv5r-mini.ufw`.
Every run writes a byte log to `build/flash-logs/`.

**Rescue route** (case open, ESP32 wiring; docs/TESTING.md): `make flashimg ...` as above, then as administrator
`E:\jl-uboot-tool\uv5r_flash_ours.ps1` (not in this repo). That script writes `build/open-uv5r-mini-flash.bin`, so build it again after every change.

## 7. Check your changes (before a commit)

```powershell
make check
python tests/update_host.py      # update mode against the real flasher
python tests/chirp_host.py       # CHIRP's own driver against our programming mode (needs a CHIRP checkout)
python tests/settings_host.py    # saving, against your flash dump if you have one
```

AGENTS.md, "Checks before you commit", has the full list.
