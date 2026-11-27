#!/bin/sh
# Copy JieLi's FM receiver and DAC libraries out of your clone of their public BR23 SDK into build/sdk/, where the
# firmware build links them for the FM radio (features.mk ENABLE_APP_FM; docs/THIRD-PARTY.md).
#
#   sh tools/sdk/fm_libs.sh <fw-AC63_BT_SDK clone>          (from the repo root; Linux, WSL, or Git Bash)
#
# Nothing is copied into git: build/ is ignored. What the build uses from them:
#   cpu.a   fm_inside_api, fm_init, fm_rf, fm_scan, fm_digital_set, fm_global_dat, fm_dem (the FM receiver), and
#           its helpers clock, efuse, p33
#   media.a audio_dac (the DAC set-up)
#   btctrler.a  the Bluetooth controller, for the BLE backend (ENABLE_BT_LE, src/platform/jl_sdk/bt_le.c)
# They are Apache-2.0 (the SDK's LICENSE, copied next to them). Tested with the SDK at commit fdc018d (2025-03-18).
set -e
SDK=${1:?usage: sh tools/sdk/fm_libs.sh <fw-AC63_BT_SDK clone>}
LIB="$SDK/cpu/br23/liba"
for f in cpu.a media.a btctrler.a; do
	[ -f "$LIB/$f" ] || { echo "not found: $LIB/$f (is $SDK a clone of Jieli-Tech/fw-AC63_BT_SDK?)" >&2; exit 1; }
done
mkdir -p build/sdk
cp "$LIB/cpu.a" "$LIB/media.a" "$LIB/btctrler.a" build/sdk/
cp "$SDK/LICENSE" build/sdk/LICENSE.jieli
( cd "$SDK" && git log -1 --format='SDK commit %h %cd' 2>/dev/null ) > build/sdk/VERSION.txt || true
echo "copied cpu.a, media.a, btctrler.a and LICENSE.jieli into build/sdk/"
