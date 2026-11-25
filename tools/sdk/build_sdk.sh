#!/bin/sh
# Build an application of JieLi's fw-AC63_BT_SDK for the AC695N (BR23) with the open pi32v2-llvm toolchain, through
# pi32v2-isa's drop-in and this repo's small C library. Linux or WSL. See tools/sdk/README.md.
#
#   tools/sdk/build_sdk.sh [--radio] SDK_DIR DROPIN_DIR [TARGET]       (TARGET default: ac635n_spp_and_le)
#
# --radio     also put our radio code in, as a task of the app, with CHIRP over Bluetooth (tools/sdk/add_radio.sh;
#             ac635n_spp_and_le only). Without it the SDK's own demo app is built.
# SDK_DIR     a clone of https://github.com/Jieli-Tech/fw-AC63_BT_SDK with Unix line endings
#             (git -c core.autocrlf=false clone ...). It is changed in place: see "SDK fixes" below.
# DROPIN_DIR  the output of pi32v2-isa's tools/dropin/make_dropin.sh
#
# The log is SDK_DIR/build-TARGET.log.
set -eu
radio=0
[ "${1:-}" = --radio ] && { radio=1; shift; }
[ $# -ge 2 ] || { sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
sdk=$(cd "$1" && pwd)
dropin=$(cd "$2" && pwd)
target=${3:-ac635n_spp_and_le}
here=$(cd "$(dirname "$0")" && pwd)

sh "$here/install_libc.sh" "$dropin"

# SDK fixes (each one small, for a difference between JieLi's compiler and the open one):
# 1. Tuya's profile reads the stack pointer through a register variable named "sp"; pi32v2-llvm has no such named
#    register. The frame address is close enough for what Tuya uses it for (a stack-depth debug print).
f="$sdk/apps/common/third_party_profile/tuya_protocol/sdk/include/tuya_ble_type.h"
if grep -q 'register unsigned sp __asm("sp");' "$f"; then
  sed -i 's|register unsigned sp __asm("sp");|unsigned sp = (unsigned)__builtin_frame_address(0); /* open-uv5r: no named sp register in pi32v2-llvm */|' "$f"
fi

# 2. Two link options of JieLi's ld that ld.lld (and the drop-in's wrapper) do not know: a stack-size warning
#    threshold, and quieting call-range warnings. ld.lld reports a call out of range as an error, which we want.
for mk in "$sdk"/apps/*/board/br23/Makefile; do
  sed -i -e '/--plugin-opt=-warn-stack-size=/d' -e '/--dont-complain-call-overflow/d' "$mk"
done

# 3. ld.lld takes OVERLAY but not SUBALIGN on it. The overlay start is ALIGN(4) already and each input section
#    keeps its own alignment, so it is dropped (the linker script is made from these files in the pre-build step).
for f in "$sdk"/cpu/br23/sdk_ld.c "$sdk"/apps/*/board/br23/app_overlay.ld; do
  [ -f "$f" ] && sed -i '/OVERLAY/s/ SUBALIGN(4)//' "$f"
done

# 4. ld.lld reads ":ALIGN(4)" (no space after the colon) as one word. A space fixes it.
#    The linker script includes .ld files from the app, include_lib/update and include_lib/driver.
for f in "$sdk"/cpu/br23/sdk_ld.c "$sdk"/apps/*/board/br23/*.ld "$sdk"/include_lib/update/update.ld \
         "$sdk"/include_lib/driver/cpu/br23/driver_lib.ld; do
  [ -f "$f" ] && sed -i 's/):ALIGN(/) : ALIGN(/g; s/ :ALIGN(/ : ALIGN(/g' "$f"
done

# 5. JieLi's prebuilt start-up code (cpu.a, startup.S, run from RAM) calls exception_analyze (cpu.a, debug.c, put
#    in flash .text) with a short call, which reaches +-2 MB; RAM is at 0 and flash at 0x1E00000. JieLi's ld is
#    told not to complain about it (fix 2). Here the call is wrapped (--wrap) to a small function in RAM code that
#    makes the long call on (tools/sdk/fixups/fixups.c, built into libopenuv5r_sdkfix.a by install_libc.sh). No
#    SDK file changes for this one.
#
# Calls between flash and RAM code need the long call form (as -mllvm -pi32v2-large-program does for the AC79
# SDK). The code is generated at link time (LTO), so the option goes to ld, through the drop-in's
# PI32V2_DROPIN_LDFLAGS (added at the end of the ld.lld command).
export PI32V2_DROPIN_LDFLAGS="${PI32V2_DROPIN_LDFLAGS:-} -mllvm=-pi32v2-large-program=true \
--wrap=exception_analyze $dropin/pi32v2/lib/libopenuv5r_sdkfix.a"

if [ $radio = 1 ]; then
  [ "$target" = ac635n_spp_and_le ] || { echo "--radio: only ac635n_spp_and_le" >&2; exit 2; }
  sh "$here/add_radio.sh" "$sdk"
fi

ulimit -n 8192 2>/dev/null || true
cd "$sdk"
elf="$sdk/cpu/br23/tools/sdk.elf"
rm -f "$elf"
jobs=$(nproc 2>/dev/null || echo 4)
log="$sdk/build-$target.log"
if make -j"$jobs" "$target" TOOL_DIR="$dropin/pi32v2/bin" > "$log" 2>&1; then
  echo "built $target (log: $log)"
elif [ -f "$elf" ] && grep -q 'download.sh' "$log"; then
  # The SDK's post-build script (cpu/br23/tools/download.sh) needs JieLi-only tools (/opt/utils/strip-ini,
  # lz4_packet, host-client) and runs from the wrong directory; it stops the same way with JieLi's own Linux tools.
  echo "linked $elf; the SDK's post-build packaging step failed as expected (log: $log)"
  echo "section images and app.bin: pi32v2-isa/tools/dropin/postbuild.sh $dropin/pi32v2/bin $elf OUTDIR"
else
  echo "build of $target failed; last lines of $log:" >&2
  tail -30 "$log" >&2
  exit 1
fi
