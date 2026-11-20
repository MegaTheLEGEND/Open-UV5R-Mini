#!/usr/bin/env python3
"""Pre-flight checks for a flashable UV-5R Mini image, before it goes near a radio.

Re-implements the image interlocks Codeplug Studio's UV-5R Mini flasher documents
(pskillen/codeplug-studio docs/reference/radios/baofeng/uv-5r-mini/firmware.md), for base nrf-v0.27:

  * Container: header, record-table and file CRCs good; exactly flash.bin and ota.bin, both inside the file
  * Loader:    ota.bin is byte for byte the loader of the reference flashable image (the pinned L2 loader)
  * Area:      flash.bin starts at SoC 0x1000 and ends at or below 0x7C000
  * Stock bytes: flash.bin has the stock length and equals the stock everywhere except the app windows
               [0x4100, 0x7BBC8), [0x4000, 0x4004), [0x4020, 0x4024) (SoC addresses)
plus every app-area CRC (tools/ufwcheck.py).

usage: python tools/flashcheck.py IMAGE.ufw --stock stock-nrf-v0.27.ufw --reference stock-rebuilt-flashable.ufw
"""
import argparse
import hashlib
import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from jl_ufw_unpack import jl_crypt  # noqa: E402
from mkufw import crc16  # noqa: E402

FLASH_SOC = 0x1000
AREA_END = 0x7C000
WINDOWS = [(0x4100, 0x7BBC8), (0x4000, 0x4004), (0x4020, 0x4024)]
L2_LOADER_SHA = '957bb486bb1d8cb5c971e366cd5c45a6da3c64fe611203096b56bb799ab02372'


def files(u):
    hdr = jl_crypt(u[:0x40])
    n = struct.unpack_from('<H', hdr, 8)[0]
    out = {}
    for i in range(n):
        r = jl_crypt(u[0x40 + 0x50 * i:0x90 + 0x50 * i])
        name = r[0x40:].split(b'\0')[0].decode('latin1')
        off, ln = struct.unpack_from('<II', r, 8)
        out[name] = (r[0], off, ln, struct.unpack_from('<H', r, 4)[0])
    return hdr, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('image')
    ap.add_argument('--stock', required=True)
    ap.add_argument('--reference', required=True, help='to-stock flashable image (stock + pinned loader)')
    a = ap.parse_args()
    img, stock, ref = (open(p, 'rb').read() for p in (a.image, a.stock, a.reference))
    ok = True

    def check(what, good, detail=''):
        nonlocal ok
        ok &= bool(good)
        print('%-12s %s %s' % (what, 'ok ' if good else 'BAD', detail))

    hdr, f = files(img)
    _, fs = files(stock)
    _, fr = files(ref)
    check('Container', set(f) == {'flash.bin', 'ota.bin'} and all(o + l <= len(img) for _, o, l, _ in f.values()),
          ', '.join('%s@0x%x+0x%x' % (k, v[1], v[2]) for k, v in f.items()))
    r = subprocess.run([sys.executable, '-I', os.path.join(os.path.dirname(__file__), 'ufwcheck.py'), a.image],
                       capture_output=True, text=True)
    check('CRCs', r.returncode == 0, '(tools/ufwcheck.py)')

    def blob(u, ff, name):
        _, o, l, _ = ff[name]
        return u[o:o + l]

    ota, ref_ota = blob(img, f, 'ota.bin'), blob(ref, fr, 'ota.bin')
    check('Loader', ota == ref_ota, 'ota.bin %d bytes, sha256 %s' % (len(ota), hashlib.sha256(ota).hexdigest()[:16]))
    fl, sfl = blob(img, f, 'flash.bin'), blob(stock, fs, 'flash.bin')
    check('Area', FLASH_SOC + len(fl) <= AREA_END, 'flash.bin SoC 0x%x..0x%x' % (FLASH_SOC, FLASH_SOC + len(fl)))
    diff_out = [i for i in range(min(len(fl), len(sfl))) if fl[i] != sfl[i]
                and not any(lo <= FLASH_SOC + i < hi for lo, hi in WINDOWS)]
    check('Stock bytes', len(fl) == len(sfl) and not diff_out,
          'same length' if len(fl) == len(sfl) else 'length %d vs stock %d' % (len(fl), len(sfl)))
    if diff_out:
        print('             first differences outside the app windows at SoC', ', '.join(hex(FLASH_SOC + i) for i in diff_out[:5]))
    print('ALL OK' if ok else 'FAILED: do not flash')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
