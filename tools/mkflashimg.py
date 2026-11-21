#!/usr/bin/env python3
"""Make a whole internal-flash image (1 MB) for writing over the ROM USB download mode.

Takes your own radio's backup (read with jl-uboot-tool, docs/RE-NOTES.md) and puts in the flash.bin of a .ufw
built by tools/mkufw.py, at flash 0x1000 where the bootloader expects it. Everything else (the voice prompts,
calibration, settings and Bluetooth info of your radio) stays as in the backup. The result holds stock code
and your radio's own data: it goes in build/ and never in git.

  python tools/mkflashimg.py --backup firmware/backups/internal-....bin --ufw build/open-uv5r-mini.ufw \
      -o build/open-uv5r-mini-flash.bin

Prints which 4 KB sectors differ from the backup, so you can see that only the app area changes.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from jl_ufw_unpack import jl_crypt  # noqa: E402

SIZE = 0x100000
FLASH_BIN_AT = 0x1000
APP_END = 0x7C000      # an update never writes past here (docs/TESTING.md)


def flash_bin(ufw):
    hdr = jl_crypt(ufw[:0x40])
    n = struct.unpack_from('<H', hdr, 8)[0]
    for i in range(n):
        r = jl_crypt(ufw[0x40 + 0x50 * i:0x90 + 0x50 * i])
        name = r[0x40:].split(b'\0')[0].decode('latin1')
        off, ln = struct.unpack_from('<II', r, 8)
        if name == 'flash.bin':
            return ufw[off:off + ln]
    raise SystemExit('no flash.bin in the .ufw')


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--backup', required=True)
    ap.add_argument('--ufw', required=True)
    ap.add_argument('-o', '--out', required=True)
    a = ap.parse_args()

    backup = open(a.backup, 'rb').read()
    if len(backup) != SIZE:
        raise SystemExit('the backup must be the whole 1 MB internal flash')
    fb = flash_bin(open(a.ufw, 'rb').read())
    if FLASH_BIN_AT + len(fb) > APP_END:
        raise SystemExit('flash.bin is too long: it would reach past 0x%X' % APP_END)

    img = bytearray(backup)
    img[FLASH_BIN_AT:FLASH_BIN_AT + len(fb)] = fb
    open(a.out, 'wb').write(img)

    diff = [s for s in range(0, SIZE, 0x1000) if img[s:s + 0x1000] != backup[s:s + 0x1000]]
    print('wrote %s; %d sectors differ from the backup' % (a.out, len(diff)))
    if diff:
        print('  from 0x%05X to 0x%05X' % (diff[0], diff[-1] + 0x1000))
    if any(s >= APP_END or s < FLASH_BIN_AT for s in diff):
        raise SystemExit('a sector outside 0x1000-0x7C000 differs: stopping')


if __name__ == '__main__':
    main()
