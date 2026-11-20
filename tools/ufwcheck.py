#!/usr/bin/env python3
"""Check every CRC in a UV-5R Mini .ufw (stock or built by mkufw.py).

usage: python tools/ufwcheck.py FILE.ufw [--app expected_app.bin]
Exit code 0 only if every checksum matches (and the app.bin slot equals --app, if given).
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from jl_ufw_unpack import jl_crypt  # noqa: E402
from mkufw import APP_AREA, APP_BIN_OFFSET, app_area_crypt, crc16  # noqa: E402


def flash_of(u):
    hdr = jl_crypt(u[:0x40])
    for i in range(struct.unpack_from('<H', hdr, 8)[0]):
        r = jl_crypt(u[0x40 + 0x50 * i:0x90 + 0x50 * i])
        if r[0x40:].split(b'\0')[0] == b'flash.bin':
            off, ln = struct.unpack_from('<II', r, 8)
            return u[off:off + ln]
    raise SystemExit('no flash.bin')


def app_entry(u):
    """(length, data CRC) of the app.bin directory entry."""
    ent = app_area_crypt(flash_of(u)[APP_AREA + 32:APP_AREA + 64], 32)
    return struct.unpack_from('<I', ent, 8)[0], struct.unpack_from('<H', ent, 2)[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ufw')
    ap.add_argument('--app')
    a = ap.parse_args()
    u = open(a.ufw, 'rb').read()
    ok = True

    def check(what, got, want):
        nonlocal ok
        good = got == want
        ok &= good
        print('%-34s %s  (0x%04X, stored 0x%04X)' % (what, 'ok ' if good else 'BAD', got, want))

    hdr = jl_crypt(u[:0x40])
    nrec = struct.unpack_from('<H', hdr, 8)[0]
    check('ufw header', crc16(hdr[2:0x40]), struct.unpack_from('<H', hdr, 0)[0])
    check('ufw record table', crc16(u[0x40:0x40 + 0x50 * nrec]), struct.unpack_from('<H', hdr, 2)[0])
    flash = None
    for i in range(nrec):
        r = jl_crypt(u[0x40 + 0x50 * i:0x90 + 0x50 * i])
        name = r[0x40:].split(b'\0')[0].decode('latin1')
        off, ln = struct.unpack_from('<II', r, 8)
        check('ufw file ' + name, crc16(u[off:off + ln]), struct.unpack_from('<H', r, 4)[0])
        if name == 'flash.bin':
            flash = u[off:off + ln]

    fh = jl_crypt(flash[:32])
    check('flash.bin header', crc16(fh[2:32]), struct.unpack_from('<H', fh, 0)[0])
    area = app_area_crypt(flash[APP_AREA:], 0)
    head, ent = area[0:32], area[32:64]
    area_len = struct.unpack_from('<I', head, 8)[0]
    app_len = struct.unpack_from('<I', ent, 8)[0]
    check('app_area_head entry', crc16(head[2:32]), struct.unpack_from('<H', head, 0)[0])
    check('app_area_head data', crc16(area[0x20:area_len]), struct.unpack_from('<H', head, 2)[0])
    check('app.bin entry', crc16(ent[2:32]), struct.unpack_from('<H', ent, 0)[0])
    app = area[APP_BIN_OFFSET:APP_BIN_OFFSET + app_len]
    check('app.bin data', crc16(app), struct.unpack_from('<H', ent, 2)[0])
    if area_len != APP_BIN_OFFSET + app_len:
        print('app area length 0x%X does not match app.bin end 0x%X' % (area_len, APP_BIN_OFFSET + app_len))
        ok = False
    if a.app:
        want = open(a.app, 'rb').read()
        same = app[:len(want)] == want and set(app[len(want):]) <= {0xFF}
        ok &= same
        print('%-34s %s' % ('app.bin slot equals ' + os.path.basename(a.app), 'ok ' if same else 'BAD'))
    print('ALL OK' if ok else 'FAILED')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
