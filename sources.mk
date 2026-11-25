# Open-UV5R-Mini: the firmware's sources (paths from the repo root), for the open build (Makefile) and the
# Bluetooth edition built inside JieLi's SDK (tools/sdk/open_uv5r.mk, which leaves out src/main.c, src/start.S and
# src/bt/bt_none.c and adds src/platform/jl_sdk/). Include features.mk first: the ENABLE_ switches leave files out.

FW_SRC := src/main.c src/board.c src/app/app.c src/radio/radio.c src/radio/txlock.c src/store/calib.c src/store/channels.c src/radio/tones.c src/store/settings.c src/app/battery.c src/radio/scan.c src/radio/dualwatch.c src/radio/vox.c src/radio/dtmfseq.c src/app/keytest.c src/app/program.c src/app/version.c src/update/update.c src/update/update_radio.c \
       src/driver/uart.c src/bt/bt_none.c src/driver/gpio.c src/driver/system.c src/driver/bk4819.c src/driver/st7735s.c src/driver/keyboard.c \
       src/driver/flash.c src/driver/io.c src/driver/adc.c src/driver/mem.c src/driver/extflash.c src/driver/spi1.c \
       src/ui/font.c src/ui/helper.c src/ui/icons.c src/ui/screen_main.c src/ui/screen_menu.c \
       src/app/apps.c src/app/spectrum.c src/app/meter.c src/app/chbook.c src/app/sysinfo.c src/app/snake.c \
       src/ui/screen_apps.c
FW_ASM := src/start.S src/update/handover.S

ifeq ($(ENABLE_KEYTEST),0)
FW_SRC := $(filter-out src/app/keytest.c,$(FW_SRC))
endif
ifeq ($(ENABLE_APP_SPECTRUM),0)
FW_SRC := $(filter-out src/app/spectrum.c,$(FW_SRC))
endif
ifeq ($(ENABLE_APP_METER),0)
FW_SRC := $(filter-out src/app/meter.c,$(FW_SRC))
endif
ifeq ($(ENABLE_APP_CHANNELS),0)
FW_SRC := $(filter-out src/app/chbook.c,$(FW_SRC))
endif
ifeq ($(ENABLE_APP_SYSINFO),0)
FW_SRC := $(filter-out src/app/sysinfo.c,$(FW_SRC))
endif
ifeq ($(ENABLE_APP_SNAKE),0)
FW_SRC := $(filter-out src/app/snake.c,$(FW_SRC))
endif
