# Open-UV5R-Mini: which features go into the firmware (as egzumer's UV-K5 Makefile does).
# 1 = built in, 0 = left out. Change a line and run make again: everything that uses it is rebuilt.
# One build without editing this file:  make ENABLE_CHIRP=0 TOOLCHAIN=...

# Programming with CHIRP over the cable in normal mode (src/app/program.c). Off: the cable UART is never set up in
# normal mode (the update mode, PTT-A + = at power-on, always has it).
ENABLE_CHIRP        ?= 1

# Start-up check of the CHIRP cable set-up: T1 BOOT, then W1 FIXED with the cable UART on (shows only if the screen
# survives it), then the CHIRP CABLE pin-check result; about 7 s. Only with ENABLE_CHIRP. Turn off once confirmed.
ENABLE_SCREEN_TEST  ?= 0

# Saving channel, VFO and settings changes to the external flash (src/store/settings.c). Off: the image has no
# external-flash erase or program code at all, and CHIRP uploads are refused.
ENABLE_SAVE         ?= 1

# Transmitting. Off: PTT never keys the transmitter. On: only in the bands allowed by default (AGENTS.md rule 5).
ENABLE_TX           ?= 1

# The LCD and external flash through SPI1's hardware and DMA, as stock (src/driver/spi1.c). It checks itself at
# power-on (the flash ID read through it) and falls back to bit-banging. Off: always bit-banged, as before.
ENABLE_LCD_SPI      ?= 1

# Region-specific transmit modes (src/radio/txlock.h): the REGION item (US, EU/UK, AU) and the TX LOCK modes that
# depend on it (GMRS, PMR446, UHF CB, and each region's amateur band edges). Off: nothing region specific in the
# image; TX LOCK offers only LOCKED, HAM (144-146 and 430-440 MHz, common to all ITU regions) and UNLOCKED.
ENABLE_REGIONS      ?= 1

# Lock the image to one region: 0 = chosen in the menu's REGION item; 1 US, 2 EU/UK, 3 AU (needs ENABLE_REGIONS=1).
REGION              ?= 0

# The UNLOCKED transmit mode (anywhere the radio has a transmit path). Off: the image cannot transmit outside the
# bands of the other modes. The default mode is always HAM.
ENABLE_TX_UNLOCK    ?= 1

# The key test (src/app/keytest.c): menu item KEY TEST, and VM, A/B or the flashlight key held at power-on. It shows
# raw key codes, PTT inputs and chip registers, for finding hardware. Off: not in the image, and the menu items
# after it move up one number.
ENABLE_KEYTEST      ?= 1

# Apps (MENU -> APPS; src/app/apps.h), one switch each. Off: the app's file is left out and its menu row goes (the
# rows after it move up one number). With all off, the APPS section itself goes.
# Spectrum: the band around the VFO frequency from the receiver's signal strength (src/app/spectrum.c).
ENABLE_APP_SPECTRUM ?= 1
# Signal meter for fox hunting: level, S-units, peak, history, tick sound (src/app/meter.c).
ENABLE_APP_METER    ?= 1
# Channel book: the channel list with names, pick one (src/app/chbook.c).
ENABLE_APP_CHANNELS ?= 1
# System info: battery, calibration, flash, channels, TX lock, uptime, build (src/app/sysinfo.c). In MENU -> ABOUT (key 1),
# not the APPS list.
ENABLE_APP_SYSINFO  ?= 1
# APRS: position beacon (AFSK from the BK4819's tone generator) and receive through the BK4819's FSK modem
# (experimental), a list of heard stations (src/app/aprs.c, src/radio/ax25.c, aprs.c, fskrx.c).
ENABLE_APP_APRS     ?= 1
# SUB-GHZ: receive only; records 315/433 MHz remote controls (OOK) from the signal strength and finds
# PT2262/EV1527 codes; debug builds hand them to the PC as Flipper .sub files (src/app/subghz.c, src/radio/ook.c).
ENABLE_APP_SUBGHZ   ?= 1
# BLE SCAN: Bluetooth Low Energy devices nearby, strongest first, with details (src/app/btscan.c, src/bt/le_adv.c).
# Receive only. Needs a Bluetooth backend that scans (src/bt/bt.h); the open build has none yet, so it says so.
ENABLE_APP_BTSCAN   ?= 1
# BT INFO: what the Bluetooth backend can do, the radio's address, the stock BTIF bytes, read only (src/app/btinfo.c).
ENABLE_APP_BTINFO   ?= 1
# BT BEACON: the radio's own BLE beacon (its name, an iBeacon, or an Eddystone URL), non-connectable, only while
# the app is open (src/app/btbeacon.c). Needs a backend that advertises; the open build has none yet.
ENABLE_APP_BTBEACON ?= 1
# BT SERIAL: a small terminal on the Bluetooth serial link (src/app/btserial.c). Needs a serial link (SDK edition).
ENABLE_APP_BTSERIAL ?= 1
# BT CHIRP: CHIRP programming over the BLE serial link (src/app/btchirp.c, the cable's protocol from
# src/app/program.c); on the PC tools/ble_serial.py bridges it to a TCP port CHIRP opens. Needs ENABLE_CHIRP and the
# BLE backend (ENABLE_BT_LE); without them the app says so.
ENABLE_APP_BTCHIRP  ?= 1
# A snake game (src/app/snake.c).
ENABLE_APP_SNAKE    ?= 1
# FM broadcast radio, 64-108 MHz through the speaker (src/app/fmradio.c, src/platform/jl_sdk/fmrx.c). Needs JieLi's
# FM and DAC libraries from their public SDK in build/sdk/ (sh tools/sdk/fm_libs.sh <sdk clone>); without them it is
# left out with a warning. The image then holds JieLi's closed (Apache-2.0) library code: docs/THIRD-PARTY.md.
ENABLE_APP_FM       ?= 1
ifeq ($(ENABLE_APP_FM),1)
ifeq ($(wildcard build/sdk/cpu.a build/sdk/media.a),)
$(warning ENABLE_APP_FM: build/sdk/cpu.a and media.a not found (tools/sdk/fm_libs.sh); the FM radio is left out)
override ENABLE_APP_FM := 0
endif
endif

# The BLE backend: Bluetooth Low Energy scanning and advertising on JieLi's own controller library, run from our
# main loop (src/platform/jl_sdk/bt_le.c, bt_glue.c), only while a Bluetooth app uses it. Needs build/sdk/btctrler.a
# (sh tools/sdk/fm_libs.sh <sdk clone>) and the FM radio's glue (ENABLE_APP_FM); without them the build has no
# Bluetooth (src/bt/bt_none.c) and the Bluetooth apps say so. Closed (Apache-2.0) library code: docs/THIRD-PARTY.md.
ENABLE_BT_LE        ?= 1
# Only paired phones and PCs may use the Bluetooth serial link (BT SERIAL, BT CHIRP): others are asked to pair, which
# works only in pairing mode (5 in those apps). 0 = pairing is offered but not needed.
ENABLE_BT_PAIRED_ONLY ?= 0
ifeq ($(ENABLE_BT_LE),1)
ifneq ($(ENABLE_APP_FM),1)
$(warning ENABLE_BT_LE needs ENABLE_APP_FM (its glue and libraries); Bluetooth is left out)
override ENABLE_BT_LE := 0
else ifeq ($(wildcard build/sdk/btctrler.a),)
$(warning ENABLE_BT_LE: build/sdk/btctrler.a not found (tools/sdk/fm_libs.sh); Bluetooth is left out)
override ENABLE_BT_LE := 0
endif
endif

# Debugging aids for bring-up: the FM app's debug lines (step trace, heartbeat, live counters), the FM cable monitor
# and the FM log on the cable UART. Crash readouts (screen and cable) stay in either way. Off for normal use.
ENABLE_DEBUGGING    ?= 0

FEATURE_FLAGS := -DPROGRAM_ENABLE=$(ENABLE_CHIRP) -DPROGRAM_SCREEN_TEST=$(ENABLE_SCREEN_TEST) \
                 -DSETTINGS_ENABLE_SAVE=$(ENABLE_SAVE) -DRADIO_ENABLE_TX=$(ENABLE_TX) -DLCD_HW_SPI=$(ENABLE_LCD_SPI) \
                 -DTXLOCK_REGIONS=$(ENABLE_REGIONS) -DTXLOCK_REGION=$(REGION) -DTXLOCK_UNLOCK=$(ENABLE_TX_UNLOCK) \
                 -DKEYTEST_ENABLE=$(ENABLE_KEYTEST) \
                 -DAPP_SPECTRUM_ENABLE=$(ENABLE_APP_SPECTRUM) -DAPP_METER_ENABLE=$(ENABLE_APP_METER) \
                 -DAPP_CHANNELS_ENABLE=$(ENABLE_APP_CHANNELS) -DAPP_SYSINFO_ENABLE=$(ENABLE_APP_SYSINFO) \
                 -DAPP_SNAKE_ENABLE=$(ENABLE_APP_SNAKE) -DAPP_FM_ENABLE=$(ENABLE_APP_FM) \
                 -DAPP_APRS_ENABLE=$(ENABLE_APP_APRS) -DAPP_SUBGHZ_ENABLE=$(ENABLE_APP_SUBGHZ) \
                 -DAPP_BTSCAN_ENABLE=$(ENABLE_APP_BTSCAN) -DAPP_BTINFO_ENABLE=$(ENABLE_APP_BTINFO) \
                 -DAPP_BTBEACON_ENABLE=$(ENABLE_APP_BTBEACON) -DAPP_BTSERIAL_ENABLE=$(ENABLE_APP_BTSERIAL) \
                 -DAPP_BTCHIRP_ENABLE=$(ENABLE_APP_BTCHIRP) -DBT_LE_ENABLE=$(ENABLE_BT_LE) -DBT_PAIRED_ONLY=$(ENABLE_BT_PAIRED_ONLY) \
                 -DDEBUGGING=$(ENABLE_DEBUGGING)
# CHIRP over the BLE serial link: the BT CHIRP app with the BLE backend and CHIRP programming
ifeq ($(ENABLE_BT_LE)$(ENABLE_APP_BTCHIRP)$(ENABLE_CHIRP),111)
FEATURE_FLAGS += -DPROGRAM_BT=1
endif
