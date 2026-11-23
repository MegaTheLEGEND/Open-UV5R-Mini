# Open-UV5R-Mini: which features go into the firmware (as egzumer's UV-K5 Makefile does).
# 1 = built in, 0 = left out. Change a line and run make again: everything that uses it is rebuilt.
# One build without editing this file:  make ENABLE_CHIRP=0 TOOLCHAIN=...

# Programming with CHIRP over the cable in normal mode (src/app/program.c). Off: the cable UART is never set up in
# normal mode (the update mode, PTT-A + = at power-on, always has it).
ENABLE_CHIRP        ?= 1

# Start-up screen test for the CHIRP cable set-up: about 16 s of labels (T1 BOOT ... T4 NONE) at every power-on.
# Only with ENABLE_CHIRP. For finding why the screen goes blank; turn off once that is fixed.
ENABLE_SCREEN_TEST  ?= 1

# Saving channel, VFO and settings changes to the external flash (src/store/settings.c). Off: the image has no
# external-flash erase or program code at all, and CHIRP uploads are refused.
ENABLE_SAVE         ?= 1

# Transmitting. Off: PTT never keys the transmitter. On: only in the bands allowed by default (AGENTS.md rule 5).
ENABLE_TX           ?= 0

FEATURE_FLAGS := -DPROGRAM_ENABLE=$(ENABLE_CHIRP) -DPROGRAM_SCREEN_TEST=$(ENABLE_SCREEN_TEST) \
                 -DSETTINGS_ENABLE_SAVE=$(ENABLE_SAVE) -DRADIO_ENABLE_TX=$(ENABLE_TX)
