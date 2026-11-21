# jl_usbkey: put the radio's chip into USB download mode with an ESP32

**Worked on one NRF V0.27 radio (2026-11-21):** the PC showed `BR23 UBOOT1.00` and a full 1 MB read was clean
(see [RE-NOTES](../../docs/RE-NOTES.md#rom-usb-download-mode-works-read-and-write)). Wired with 220 Ω resistors.
The goal is to read a backup of the AC695N's internal flash (firmware, calibration,
settings) over USB, with kagaimiq's [jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool), which lists the
AC695N (BR23) as working. Read only: don't write or erase anything with it.

The AC695N's mask ROM has a USB download mode. To start it, something must send a 16-bit key (`0x16EF`) on the USB
lines while the chip boots: D+ is the clock, D− the data. The chip answers by pulling both lines low for 1–2 ms,
then pulls D+ up and waits for the PC. Protocol from kagaimiq's notes:
[USB_KEY](https://kagaimiq.github.io/jielie/isp/usb/usb-key.html). A JieLi "forced download" dongle does this; this
ESP32 sketch does the same job.

## Wiring (case open, battery in)

The pads are measured: see [RE-NOTES](../../docs/RE-NOTES.md#measured-with-a-multimeter-v16-board-battery-out).

| Radio pad | USB cable to the PC | ESP32 dev module |
| --- | --- | --- |
| `G` (ground) | black | GND |
| `P` (D+) | green | GPIO25, through a 1 kΩ resistor |
| `M` (D−) | white | GPIO26, through a 1 kΩ resistor |
| `V` | red: **not connected**, taped off | not connected |

- Plug the radio's USB cable straight into the PC, not into a hub, and keep other USB devices off that port.
- The resistors protect both chips while the radio is off and while the chip pulls the lines low. They also keep
  the ESP32's wires from upsetting USB once it lets go.
- Use a 3.3 V board. Never a 5 V Arduino.

## Use

```powershell
cd tools/jl_usbkey
pio run -t upload       # into the ESP32
pio device monitor      # watch its messages
```

1. Radio off.
2. Press **BOOT** on the ESP32. Its blue LED lights while it sends the key.
3. Switch the radio on within 5 s.
4. The ESP32 says whether D+ went high, then lets go of the lines.
5. On the PC, a new USB device whose name starts with `UBOOT` means it worked. "Unknown USB Device" means the radio
   started its normal app instead: switch it off and try again.

D+ also goes high when the normal app starts, so the ESP32 can't tell the two apart; the PC's device list can.
