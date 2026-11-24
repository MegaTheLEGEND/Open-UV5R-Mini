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
# System info: battery, calibration, flash, channels, TX lock, uptime, build (src/app/sysinfo.c).
ENABLE_APP_SYSINFO  ?= 1
# A snake game (src/app/snake.c).
ENABLE_APP_SNAKE    ?= 1

FEATURE_FLAGS := -DPROGRAM_ENABLE=$(ENABLE_CHIRP) -DPROGRAM_SCREEN_TEST=$(ENABLE_SCREEN_TEST) \
                 -DSETTINGS_ENABLE_SAVE=$(ENABLE_SAVE) -DRADIO_ENABLE_TX=$(ENABLE_TX) -DLCD_HW_SPI=$(ENABLE_LCD_SPI) \
                 -DTXLOCK_REGIONS=$(ENABLE_REGIONS) -DTXLOCK_REGION=$(REGION) -DTXLOCK_UNLOCK=$(ENABLE_TX_UNLOCK) \
                 -DKEYTEST_ENABLE=$(ENABLE_KEYTEST) \
                 -DAPP_SPECTRUM_ENABLE=$(ENABLE_APP_SPECTRUM) -DAPP_METER_ENABLE=$(ENABLE_APP_METER) \
                 -DAPP_CHANNELS_ENABLE=$(ENABLE_APP_CHANNELS) -DAPP_SYSINFO_ENABLE=$(ENABLE_APP_SYSINFO) \
                 -DAPP_SNAKE_ENABLE=$(ENABLE_APP_SNAKE)
