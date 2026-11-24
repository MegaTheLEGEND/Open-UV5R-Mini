# Open-UV5R-Mini build. Needs only the open pi32v2 LLVM toolchain
# (https://github.com/jvitkauskas/pi32v2-llvm/releases).
#
#   make TOOLCHAIN=/path/to/pi32v2-llvm/bin
#
# Output: build/fw/firmware.elf and build/fw/firmware.bin (raw image for the app.bin slot at 0x01E00100).
#
# This is the fully open build, without Bluetooth. The optional Bluetooth edition is built inside your own
# JieLi SDK copy instead; see docs/BLUETOOTH.md.

TOOLCHAIN ?=
CC      := $(if $(TOOLCHAIN),$(TOOLCHAIN)/,)clang
LD      := $(if $(TOOLCHAIN),$(TOOLCHAIN)/,)ld.lld
OBJCOPY := $(if $(TOOLCHAIN),$(TOOLCHAIN)/,)llvm-objcopy
SIZE    := $(if $(TOOLCHAIN),$(TOOLCHAIN)/,)llvm-size

BUILD := build/fw
TARGET := $(BUILD)/firmware

SRC := src/main.c src/board.c src/app/app.c src/radio/radio.c src/radio/txlock.c src/store/calib.c src/store/channels.c src/radio/tones.c src/store/settings.c src/app/battery.c src/radio/scan.c src/radio/dualwatch.c src/radio/vox.c src/radio/dtmfseq.c src/app/keytest.c src/app/program.c src/app/version.c src/update/update.c src/update/update_radio.c \
       src/driver/uart.c src/bt/bt_none.c src/driver/gpio.c src/driver/system.c src/driver/bk4819.c src/driver/st7735s.c src/driver/keyboard.c \
       src/driver/flash.c src/driver/io.c src/driver/adc.c src/driver/mem.c src/driver/extflash.c src/driver/spi1.c \
       src/ui/font.c src/ui/helper.c src/ui/icons.c src/ui/screen_main.c
ASM := src/start.S src/update/handover.S

include features.mk
ifeq ($(ENABLE_KEYTEST),0)
SRC := $(filter-out src/app/keytest.c,$(SRC))
endif

CFLAGS := --target=pi32v2 -Os -ffreestanding -fno-builtin -ffunction-sections -fdata-sections \
          -std=c11 -Wall -Wextra -Isrc $(FEATURE_FLAGS)
LDFLAGS := -T src/firmware.ld --gc-sections

OBJ := $(addprefix $(BUILD)/,$(SRC:.c=.o) $(ASM:.S=.o))

all: $(TARGET).bin

# The build id in MENU -> ABOUT: the git commit, "-dirty" with uncommitted changes. version.o is always rebuilt.
FW_BUILD := $(shell git describe --always --dirty --abbrev=7 2>/dev/null || echo dev)
$(BUILD)/src/app/version.o: CFLAGS += -DFW_BUILD='"$(FW_BUILD)"'
$(BUILD)/src/app/version.o: FORCE
FORCE:

# -MMD -MP: each object also depends on the headers it includes, so changing a switch in a header (such as
# PROGRAM_ENABLE in app/program.h) rebuilds every file that uses it. Without this a stale app.o was linked.
# A switch changed in features.mk or on the command line: the flags file changes and everything is rebuilt.
FLAGS_FILE := $(BUILD)/flags.txt
$(shell mkdir -p $(BUILD); echo '$(FEATURE_FLAGS)' | cmp -s - $(FLAGS_FILE) || echo '$(FEATURE_FLAGS)' > $(FLAGS_FILE))

$(BUILD)/%.o: %.c $(FLAGS_FILE)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(OBJ:.o=.d)

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) --target=pi32v2 -c $< -o $@

$(TARGET).elf: $(OBJ) src/firmware.ld
	$(LD) $(LDFLAGS) $(OBJ) -o $@
	$(SIZE) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

# The update file: our app in the app slot of the stock .ufw with the stock loader added (docs/BUILDING.md).
# The base is made once from the two downloaded files in firmware/ (kept outside git):
#   the stock file                 firmware/stock-nrf-v0.27.ufw
#   MM9PDY's to-stock patch        firmware/uv5rmini-fw-to-stock-nrf-v0.27-hw1.7-1.1.0.patch.json (adds the loader)
PYTHON ?= python
STOCK_UFW ?= firmware/stock-nrf-v0.27.ufw
TO_STOCK_PATCH ?= firmware/uv5rmini-fw-to-stock-nrf-v0.27-hw1.7-1.1.0.patch.json
REF_UFW := build/stock-flashable-reference.ufw
BASE_UFW ?= $(REF_UFW)
UFW := build/open-uv5r-mini.ufw

# Icons: assets/icons/*.txt -> src/ui/icons.c / icons.h (generated, committed; docs/LAYOUT.md).
assets:
	$(PYTHON) tools/mkassets.py

$(REF_UFW): $(STOCK_UFW) $(TO_STOCK_PATCH)
	@mkdir -p build
	$(PYTHON) tools/ufwpatch.py apply $(TO_STOCK_PATCH) $(STOCK_UFW) -o $@

ufw: $(TARGET).bin $(BASE_UFW)
	$(PYTHON) tools/mkufw.py --base $(BASE_UFW) --app $(TARGET).bin -o $(UFW)
	$(PYTHON) tools/ufwcheck.py $(UFW) --app $(TARGET).bin

# The whole-flash image for the rescue route (uv5r_flash_ours.ps1): your own backup with our app put in.
#   make flashimg BACKUP=firmware/backups/internal-....bin TOOLCHAIN=...
flashimg: ufw
	@test -n "$(BACKUP)" || { echo 'flashimg needs BACKUP=firmware/backups/internal-....bin'; exit 1; }
	$(PYTHON) tools/mkflashimg.py --backup $(BACKUP) --ufw $(UFW) -o build/open-uv5r-mini-flash.bin

# Host syntax check with any GCC/Clang (no pi32v2 toolchain needed).
check:
	$(PYTHON) tools/mkassets.py --check
	gcc -fsyntax-only -std=c11 -Wall -Wextra -Isrc $(FEATURE_FLAGS) $(SRC)

clean:
	rm -rf $(BUILD)

# Stock v0.27 rebuilt from source byte for byte, plus a flashable image and flasher patch (docs/MATCHING.md).
ISA ?= ../pi32v2-isa
matching:
	$(PYTHON) tools/build_matching.py --toolchain $(TOOLCHAIN) --isa $(ISA)

.PHONY: all assets check clean ufw flashimg matching FORCE
