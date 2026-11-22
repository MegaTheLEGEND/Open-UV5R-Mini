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

SRC := src/main.c src/board.c src/app/app.c src/radio/radio.c src/store/calib.c src/store/channels.c src/radio/tones.c src/store/settings.c src/app/battery.c src/radio/scan.c src/radio/dualwatch.c src/radio/vox.c src/radio/dtmfseq.c src/app/keytest.c src/update/update.c src/update/update_radio.c \
       src/driver/uart.c src/bt/bt_none.c src/driver/gpio.c src/driver/system.c src/driver/bk4819.c src/driver/st7735s.c src/driver/keyboard.c \
       src/driver/flash.c src/driver/io.c src/driver/adc.c src/driver/mem.c src/driver/extflash.c \
       src/ui/font.c src/ui/helper.c src/ui/icons.c src/ui/screen_main.c
ASM := src/start.S src/update/handover.S

CFLAGS := --target=pi32v2 -Os -ffreestanding -fno-builtin -ffunction-sections -fdata-sections \
          -std=c11 -Wall -Wextra -Isrc
LDFLAGS := -T src/firmware.ld --gc-sections

OBJ := $(addprefix $(BUILD)/,$(SRC:.c=.o) $(ASM:.S=.o))

all: $(TARGET).bin

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) --target=pi32v2 -c $< -o $@

$(TARGET).elf: $(OBJ) src/firmware.ld
	$(LD) $(LDFLAGS) $(OBJ) -o $@
	$(SIZE) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

# Our app in the app.bin slot of your stock .ufw (kept outside git). NOT flashable: the stock .ufw has no loader,
# and this app has no update mode (docs/TESTING.md). For radio testing use: make matching
#   make ufw BASE_UFW=firmware/stock-nrf-v0.27.ufw
BASE_UFW ?= firmware/stock-nrf-v0.27.ufw
PYTHON ?= python
# Icons: assets/icons/*.txt -> src/ui/icons.c / icons.h (generated, committed; docs/LAYOUT.md).
assets:
	$(PYTHON) tools/mkassets.py

ufw: $(TARGET).bin
	$(PYTHON) tools/mkufw.py --base $(BASE_UFW) --app $(TARGET).bin -o build/open-uv5r-mini.ufw
	$(PYTHON) tools/ufwcheck.py build/open-uv5r-mini.ufw --app $(TARGET).bin

# Host syntax check with any GCC/Clang (no pi32v2 toolchain needed).
check:
	$(PYTHON) tools/mkassets.py --check
	gcc -fsyntax-only -std=c11 -Wall -Wextra -Isrc $(SRC)

clean:
	rm -rf $(BUILD)

# Stock v0.27 rebuilt from source byte for byte, plus a flashable image and flasher patch (docs/MATCHING.md).
ISA ?= ../pi32v2-isa
matching:
	$(PYTHON) tools/build_matching.py --toolchain $(TOOLCHAIN) --isa $(ISA)

.PHONY: all assets check clean ufw matching
