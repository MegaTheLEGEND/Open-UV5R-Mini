#!/bin/sh
# Put this repo's small C library (tools/sdk/libc) into a pi32v2-llvm drop-in directory, where JieLi's BR23 SDK
# looks for its C library: headers in pi32v2/include, libc.a and libm.a in pi32v2/lib/r3 (and the other rN).
#
#   tools/sdk/install_libc.sh DROPIN_DIR
#
# DROPIN_DIR is the output of pi32v2-isa's tools/dropin/make_dropin.sh (the directory with pi32v2/bin).
set -eu
[ $# -eq 1 ] || { sed -n '2,7p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
out=$(cd "$1" && pwd)
here=$(cd "$(dirname "$0")" && pwd)
cc="$out/llvm/bin/clang"
ar="$out/llvm/bin/llvm-ar"
[ -x "$cc" ] || { echo "install_libc: $cc missing" >&2; exit 1; }
cp -R "$here/libc/include/." "$out/pi32v2/include/"
tmp=$(mktemp -d)
for f in "$here"/libc/src/*.c; do
  "$cc" --target=pi32v2 -mcpu=r3 -Os -ffreestanding -fno-builtin -ffunction-sections -fdata-sections \
    -I"$here/libc/include" -c "$f" -o "$tmp/$(basename "$f" .c).o"
done
"$ar" rcs "$tmp/libc.a" "$tmp"/string.o "$tmp"/stdlib.o "$tmp"/time.o
"$ar" rcs "$tmp/libm.a" "$tmp"/math.o
for d in . r1 r2 r3 r4 r5; do
  mkdir -p "$out/pi32v2/lib/$d"
  cp "$tmp/libc.a" "$tmp/libm.a" "$out/pi32v2/lib/$d/"
done
# the link fix-ups (build_sdk.sh, fix 5): not part of the C library, kept in their own archive
"$cc" --target=pi32v2 -mcpu=r3 -Os -flto -c "$here/fixups/fixups.c" -o "$tmp/fixups.o"
rm -f "$out/pi32v2/lib/libopenuv5r_sdkfix.a"
"$ar" rcs "$out/pi32v2/lib/libopenuv5r_sdkfix.a" "$tmp/fixups.o"
rm -rf "$tmp"
echo "installed into $out/pi32v2/include and $out/pi32v2/lib/r3"
