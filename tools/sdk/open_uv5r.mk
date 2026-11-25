# Open-UV5R-Mini's radio code inside JieLi's SDK app (the Bluetooth edition, docs/BLUETOOTH.md).
#
# tools/sdk/add_radio.sh copies this repo's src/, features.mk, sources.mk and this file to SDK/open_uv5r/ and adds
#     include ../../../../open_uv5r/open_uv5r.mk
# to the app's board Makefile (apps/spp_and_le/board/br23/Makefile), just before it turns c_SRC_FILES into objects.
# Paths are from that Makefile's folder; ROOT_PREFIX (../../../..) is the SDK root.
#
# Our files get their own compile rule: our feature switches, PROGRAM_BT=1 (CHIRP over Bluetooth, app/program.c) and
# our src/ first on the include path, then the SDK's (the platform files use typedef.h, spp_user.h, os/os_api.h).
# Feature switches work as in the open build: make ac635n_spp_and_le ENABLE_APP_SNAKE=0 ...

OU := $(ROOT_PREFIX)/open_uv5r
include $(OU)/features.mk
include $(OU)/sources.mk

# Not here: the bare-metal entry and start-up (the SDK has its own), and the no-Bluetooth stubs.
OU_SRC := $(filter-out src/main.c src/bt/bt_none.c,$(FW_SRC)) \
          src/platform/jl_sdk/bt_spp.c src/platform/jl_sdk/radio_task.c
OU_ASM := $(filter-out src/start.S,$(FW_ASM))

c_SRC_FILES += $(addprefix $(OU)/,$(OU_SRC))
S_SRC_FILES += $(addprefix $(OU)/,$(OU_ASM))

OU_BUILD := $(shell cd $(OU) && cat build-id 2>/dev/null || echo dev)
OU_FLAGS := $(FEATURE_FLAGS) -DPROGRAM_BT=1 -DFW_BUILD='"$(OU_BUILD)"' -I$(OU)/src

$(BUILD_DIR)/open_uv5r/%.c.o : $(ROOT_PREFIX)/open_uv5r/%.c $(OU)/features.mk
	$(info +CC $<)
	$(QUITE) $(MKDIR) $(@D)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(OU_FLAGS) $(INCLUDES) -MM -MT $@ $< -o $(@:.o=.d)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(OU_FLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/open_uv5r/%.S.o : $(ROOT_PREFIX)/open_uv5r/%.S
	$(info +AS $<)
	$(QUITE) $(MKDIR) $(@D)
	$(QUITE) $(CC) --target=pi32v2 -c $< -o $@
