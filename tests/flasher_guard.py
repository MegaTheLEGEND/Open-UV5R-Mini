#!/usr/bin/env python3
"""Roadmap 1.9: tests of the flasher's update-mode guard (tools/uv5r_flasher.py), on the PC.

    python tests/flasher_guard.py

Checks that the marker our firmware carries (app/update_radio.c) names exactly the windows driver/flash.c may
write, that the guard accepts it only with the rescue warning, refuses markers that reach into VM or the rest of
PRCT, and refuses an app with neither marker nor the stock update mode. With your own stock file in firmware/, it
also checks that the stock app passes as the stock update mode and that a change in its update code is caught.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import uv5r_flasher as F  # noqa: E402

fails = 0


def check(cond, what):
    global fails
    print('%-60s %s' % (what, 'ok' if cond else 'FAIL'))
    fails += not cond


def main():
    src = open(os.path.join(ROOT, 'app', 'update_radio.c'), encoding='utf-8').read()
    m = re.search(r'"(OPENUV5R-UPDATE-MODE/1 writes=[^"]*)"', src)
    check(m is not None, 'marker found in app/update_radio.c')
    marker = m.group(1).encode() if m else b''

    flash = open(os.path.join(ROOT, 'driver', 'flash.c'), encoding='utf-8').read()
    w = re.search(r'#define WRITABLE\(a, n\) \((.*)\)\s*$', flash, re.M)
    wins = sorted((int(lo, 16), int(hi, 16)) for lo, hi in
                  re.findall(r'IN_AREA\(a, n, 0x([0-9A-Fa-f]+)u, 0x([0-9A-Fa-f]+)u\)', w.group(1) if w else ''))
    spec = marker.split(b'=', 1)[1].rstrip(b';').decode() if marker else ''
    mwins = sorted(tuple(int(x, 16) for x in s.split('-')) for s in spec.split(',') if s)
    check(wins and wins == mwins, 'marker windows = driver/flash.c WRITABLE %s' % wins)

    app = b'\0' * 64 + marker + b'\0' * 64
    kind, detail = F.update_mode_status(app, None)
    check(kind == 'ours', 'our marker recognised: %s' % detail)
    checks = [('Update mode', kind != 'none', detail)]
    check(F.needs_rescue_warning(checks), 'our update mode still needs the rescue warning')
    for bad in (b'writes=1000-7C000,94000-FE000;', b'writes=1000-94000;', b'writes=FA000-FF000;', b'writes=;',
                b'writes=12;'):
        k, d = F.update_mode_status(b'OPENUV5R-UPDATE-MODE/1 ' + bad, None)
        check(k == 'none', 'refused: %s' % bad.decode())
    check(F.update_mode_status(b'\0' * 1000, None)[0] == 'none', 'no marker, no stock code: refused')
    check(F.update_mode_status(b'', None)[0] == 'none', 'no app: refused')

    stock = os.path.join(ROOT, 'firmware', 'stock-nrf-v0.27.ufw')
    if os.path.exists(stock):
        st = open(stock, 'rb').read()
        _, f = F.ufw_files(st)
        o, n, _ = f['flash.bin']
        _, sapp, _ = F.area_report(st[o:o + n])
        check(F.update_mode_status(sapp, sapp)[0] == 'stock', 'stock app: stock update mode')
        for lo, hi in F.STOCK_UPDATE_CODE:
            b = bytearray(sapp)
            b[(lo + hi) // 2 - F.APP_BASE] ^= 0x01
            if F.update_mode_status(bytes(b), sapp)[0] != 'none':
                check(False, 'change at 0x%08X caught' % ((lo + hi) // 2))
                break
        else:
            check(True, 'a change in any stock update-mode range is caught')
    else:
        print('(no firmware/stock-nrf-v0.27.ufw: stock checks skipped)')

    print('ALL OK' if not fails else '%d FAILED' % fails)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
