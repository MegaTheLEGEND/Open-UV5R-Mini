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

SRC := main.c board.c app/app.c bt/bt_none.c \
       driver/gpio.c driver/system.c driver/bk4819.c driver/st7735s.c driver/keyboard.c \
       ui/font.c ui/helper.c
ASM := start.S

CFLAGS := --target=pi32v2 -Os -ffreestanding -fno-builtin -ffunction-sections -fdata-sections \
          -std=c11 -Wall -Wextra -I.
LDFLAGS := -T firmware.ld --gc-sections

OBJ := $(addprefix $(BUILD)/,$(SRC:.c=.o) $(ASM:.S=.o))

all: $(TARGET).bin

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) --target=pi32v2 -c $< -o $@

$(TARGET).elf: $(OBJ) firmware.ld
	$(LD) $(LDFLAGS) $(OBJ) -o $@
	$(SIZE) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

# Flashable update: our app in the app.bin slot of your stock .ufw (kept outside git).
#   make ufw BASE_UFW=firmware/stock-nrf-v0.27.ufw
BASE_UFW ?= firmware/stock-nrf-v0.27.ufw
PYTHON ?= python
ufw: $(TARGET).bin
	$(PYTHON) tools/mkufw.py --base $(BASE_UFW) --app $(TARGET).bin -o build/open-uv5r-mini.ufw
	$(PYTHON) tools/ufwcheck.py build/open-uv5r-mini.ufw --app $(TARGET).bin

# Host syntax check with any GCC/Clang (no pi32v2 toolchain needed).
check:
	gcc -fsyntax-only -std=c11 -Wall -Wextra -I. $(SRC)

clean:
	rm -rf $(BUILD)

.PHONY: all check clean ufw
