#!/bin/sh
# Put Open-UV5R-Mini's radio code into a clone of JieLi's fw-AC63_BT_SDK as a task of its spp_and_le app: the
# "Bluetooth edition" (docs/BLUETOOTH.md), with CHIRP / stock CPS programming over Bluetooth SPP ("Wireless Cps").
# Linux or WSL. Run before tools/sdk/build_sdk.sh (which runs it for you with --radio).
#
#   tools/sdk/add_radio.sh SDK_DIR
#
# Changes the SDK clone in place (nothing from it comes into this repo), each step safe to repeat:
#   1. copies this repo's src/, features.mk, sources.mk and tools/sdk/open_uv5r.mk to SDK_DIR/open_uv5r/
#      (with a build-id file: our git commit, as MENU -> ABOUT shows it);
#   2. the app's BR23 board Makefile includes open_uv5r.mk (our sources, flags and compile rule);
#   3. the BR23 board configuration: classic Bluetooth (EDR) on, so the library's SPP service runs (SDP name
#      "JL_SPP"); the board's keys, code switch and ADC keys off, so the SDK does not drive the pins our drivers own;
#      low-power sleep off (the chip must not power down while the radio task runs);
#   4. the app's start calls open_uv5r_start() right after btstack_init() (our radio task, radio_task.c);
#   5. the SDK's SPP module (a loopback demo) hands SPP to us when the stack is up (open_uv5r_spp_attach, bt_spp.c).
set -eu
[ $# -ge 1 ] || { sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
sdk=$(cd "$1" && pwd)
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
app="$sdk/apps/spp_and_le"
mk="$app/board/br23/Makefile"
cfg="$app/board/br23/board_ac635n_demo_cfg.h"
start="$app/examples/trans_data/app_spp_and_le.c"
spp="$app/modules/bt/spp_trans.c"
for f in "$mk" "$cfg" "$start" "$spp"; do
  [ -f "$f" ] || { echo "not found: $f (is $sdk a fw-AC63_BT_SDK clone?)" >&2; exit 1; }
done

# 1. our sources
ou="$sdk/open_uv5r"
mkdir -p "$ou"
rm -rf "$ou/src"
cp -a "$repo/src" "$ou/src"
for f in "$repo/features.mk" "$repo/sources.mk" "$here/open_uv5r.mk"; do   # Unix line endings for make
  tr -d '\r' < "$f" > "$ou/$(basename "$f").new"
  cmp -s "$ou/$(basename "$f").new" "$ou/$(basename "$f")" 2>/dev/null && rm "$ou/$(basename "$f").new" ||
    mv "$ou/$(basename "$f").new" "$ou/$(basename "$f")"
done
(cd "$repo" && git describe --always --dirty --abbrev=7 2>/dev/null || echo dev) | sed 's/$/-bt/' > "$ou/build-id.new"
cmp -s "$ou/build-id.new" "$ou/build-id" 2>/dev/null && rm "$ou/build-id.new" || mv "$ou/build-id.new" "$ou/build-id"
touch -r "$ou/build-id" "$ou/src/app/version.c"   # the build id is compiled into version.c

# 2. the Makefile
if ! grep -q 'open_uv5r/open_uv5r.mk' "$mk"; then
  sed -i 's|^c_OBJS    := \$(c_SRC_FILES:%.c=%.c.o)$|include ../../../../open_uv5r/open_uv5r.mk # open-uv5r\n&|' "$mk"
  grep -q 'open_uv5r/open_uv5r.mk' "$mk" || { echo "could not add the include to $mk" >&2; exit 1; }
fi

# 3. the board: EDR (classic Bluetooth, SPP) on; keys off (our keyboard driver owns PB6/PB7 and the key pins)
sed -i -e 's|^\(#define TCFG_USER_EDR_ENABLE  *\)0\(.*\)$|\11\2 /* open-uv5r: SPP for Wireless CPS */|' \
       -e 's|^\(#define TCFG_CODE_SWITCH_ENABLE  *\)ENABLE_THIS_MOUDLE\(.*\)$|\1DISABLE_THIS_MOUDLE\2 /* open-uv5r */|' \
       -e 's|^\(#define TCFG_IOKEY_ENABLE  *\)ENABLE_THIS_MOUDLE\(.*\)$|\1DISABLE_THIS_MOUDLE\2 /* open-uv5r */|' \
       -e 's|^\(#define TCFG_ADKEY_ENABLE  *\)ENABLE_THIS_MOUDLE\(.*\)$|\1DISABLE_THIS_MOUDLE\2 /* open-uv5r */|' \
       -e 's|^\(#define TCFG_LOWPOWER_LOWPOWER_SEL\s\s*\)SLEEP_EN\(.*\)$|\10\2 /* open-uv5r: no power-down under the radio task */|' \
       "$cfg"
grep -q '^#define TCFG_USER_EDR_ENABLE  *1' "$cfg" || { echo "could not turn EDR on in $cfg" >&2; exit 1; }

# 4. start our task after the Bluetooth stack
if ! grep -q 'open_uv5r_start' "$start"; then
  sed -i 's|^\( *\)btstack_init();$|&\n\1{ extern void open_uv5r_start(void); open_uv5r_start(); } /* open-uv5r: the radio task */|' "$start"
  grep -q 'open_uv5r_start' "$start" || { echo "could not add open_uv5r_start to $start" >&2; exit 1; }
fi

# 5. SPP to us once the stack is up (after the demo's own callbacks are registered)
if ! grep -q 'open_uv5r_spp_attach' "$spp"; then
  sed -i 's|^\( *\)spp_api->regist_wakeup_send(NULL, transport_spp_send_wakeup);$|&\n\1{ extern void open_uv5r_spp_attach(void); open_uv5r_spp_attach(); } /* open-uv5r: Wireless CPS */|' "$spp"
  grep -q 'open_uv5r_spp_attach' "$spp" || { echo "could not add open_uv5r_spp_attach to $spp" >&2; exit 1; }
fi
echo "open-uv5r radio task added to $sdk (build id $(cat "$ou/build-id"))"
