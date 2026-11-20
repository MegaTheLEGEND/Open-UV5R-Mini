#!/usr/bin/env python3
"""Apply or make UV-5R Mini firmware patches in the "baofeng-uv5rmini-firmware" format 1.

The format is the one Codeplug Studio's firmware flasher reads (documented in pskillen/codeplug-studio,
docs/reference/radios/baofeng/uv-5r-mini/firmware.md): ordered in-place edits over the stock .ufw, a cut and
appended bytes, with SHA-256s pinning the stock and the result. This is an independent implementation.

usage:
  python tools/ufwpatch.py apply PATCH.json STOCK.ufw -o RESULT.ufw
  python tools/ufwpatch.py make STOCK.ufw RESULT.ufw -o PATCH.json --base nrf-v0.27 --name open-uv5r-0.1
        [--loader-sha256 HEX]
"""
import argparse
import hashlib
import json
import struct
import sys

MAX_RESULT = 1 << 20
MAX_EDITS = 4096


def sha(b):
    return hashlib.sha256(b).hexdigest()


def apply_patch(p, stock):
    if p.get('name') != 'baofeng-uv5rmini-firmware' or p.get('format') != 1:
        raise SystemExit('not a baofeng-uv5rmini-firmware format 1 patch')
    st, rs = p['stock']['.ufw'], p['result']['.ufw']
    if len(stock) != st['size'] or sha(stock) != st['sha256']:
        raise SystemExit('stock file does not match the patch (size %d, sha256 %s)' % (len(stock), sha(stock)))
    trunc = p['truncate']
    if trunc > len(stock) or len(p['edits']) > MAX_EDITS:
        raise SystemExit('patch malformed')
    out = bytearray(stock)
    last = 0
    for e in p['edits']:
        data = bytes.fromhex(e['hex'])
        if not data or e['offset'] < last or e['offset'] + len(data) > trunc:
            raise SystemExit('patch malformed: edit at %#x' % e['offset'])
        out[e['offset']:e['offset'] + len(data)] = data
        last = e['offset'] + len(data)
    out = out[:trunc] + bytes.fromhex(p['append'])
    if len(out) > MAX_RESULT or len(out) != rs['size'] or sha(out) != rs['sha256']:
        raise SystemExit('result does not match the patch (size %d, sha256 %s)' % (len(out), sha(out)))
    return bytes(out)


def make_patch(stock, result, base, name, loader_sha=None, loader_list=None):
    common = min(len(stock), len(result))
    # truncate at the first byte after which result and stock differ only by an appended tail
    trunc = common
    edits = []
    i = 0
    while i < trunc:
        if stock[i] != result[i]:
            j = i
            while j < trunc and (stock[j] != result[j] or (j + 1 < trunc and stock[j + 1] != result[j + 1])):
                j += 1
            edits.append({'offset': i, 'hex': result[i:j].hex()})
            i = j
        else:
            i += 1
    append = result[trunc:]
    if len(edits) > MAX_EDITS:
        raise SystemExit('too many edits (%d)' % len(edits))
    # app.bin directory entry (decrypted) for the descriptive build fields
    sys.path.insert(0, __file__.rsplit('/', 1)[0].rsplit('\\', 1)[0])
    from ufwcheck import app_entry
    app_size, app_crc = app_entry(result)
    p = {
        'name': 'baofeng-uv5rmini-firmware',
        'format': 1,
        'version': name,
        'base': base,
        'stock': {'.ufw': {'sha256': sha(stock), 'size': len(stock)}},
        'result': {'.ufw': {'sha256': sha(result), 'size': len(result)}},
        'build': {'name': name, 'appSize': '0x%x' % app_size, 'appCrc': '0x%x' % app_crc},
        'loader': {'sha256': loader_sha or '', 'list': loader_list or []},
        'edits': edits,
        'truncate': trunc,
        'append': append.hex(),
    }
    if not (1 <= len(name) <= 21 and name.isprintable() and ' ' not in name):
        raise SystemExit('build name must be 1..21 printable characters with no space')
    assert apply_patch(p, stock) == result
    return p


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd', required=True)
    a1 = sub.add_parser('apply')
    a1.add_argument('patch'); a1.add_argument('stock'); a1.add_argument('-o', '--out', required=True)
    a2 = sub.add_parser('make')
    a2.add_argument('stock'); a2.add_argument('result'); a2.add_argument('-o', '--out', required=True)
    a2.add_argument('--base', default='nrf-v0.27'); a2.add_argument('--name', required=True)
    a2.add_argument('--loader-sha256')
    a = ap.parse_args()
    if a.cmd == 'apply':
        p = json.load(open(a.patch, encoding='utf-8'))
        out = apply_patch(p, open(a.stock, 'rb').read())
        open(a.out, 'wb').write(out)
        print('wrote %s (%d bytes, sha256 %s) - matches the patch result' % (a.out, len(out), sha(out)))
    else:
        p = make_patch(open(a.stock, 'rb').read(), open(a.result, 'rb').read(), a.base, a.name,
                       a.loader_sha256)
        json.dump(p, open(a.out, 'w', encoding='utf-8'), indent=1)
        print('wrote %s: %d edits, cut at %d, %d bytes appended' % (a.out, len(p['edits']), p['truncate'],
                                                                     len(p['append']) // 2))


if __name__ == '__main__':
    main()
