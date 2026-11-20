#!/usr/bin/env python3
"""Build a flashable .ufw for the Baofeng UV-5R Mini by putting a new app image into a stock .ufw.

usage:
  python tools/mkufw.py --base stock-nrf-v0.27.ufw --app build/fw/firmware.bin -o build/open-uv5r-mini.ufw

Everything outside the app.bin slot (bootloader, isd_config, key_mac, ota.bin, the VM settings area and the
resource directory after the app area) is kept byte for byte. The stock .ufw itself is never modified.

Format (docs/FIRMWARE.md):
  * cipher: JieLi ENC, XOR with a CRC16-CCITT LFSR keystream (jl_crypt)
  * checksums: CRC16-CCITT, poly 0x1021, init 0 (XMODEM)
  * .ufw: 0x40-byte header (one ENC block, key 0xFFFF): +0 CRC of header bytes 2..63 (decrypted),
          +2 CRC of the record table 0x40..0xE0 (as stored); records of 0x50 bytes (each its own ENC block):
          +4 CRC of the file's bytes (as stored), +8 offset, +12 length, +0x40 name
  * flash.bin: app area at 0x3000, each 32-byte block encrypted with key 0xB50F ^ ((addr - 0x3000) >> 2)
  * app area (decrypted): entry 0 app_area_head, entry 1 app.bin, 32 bytes each:
          +0 CRC of entry bytes 2..31, +2 data CRC, +4 address/offset, +8 length
          app_area_head data CRC = CRC of area bytes 0x20..length; app.bin data CRC = CRC of its bytes
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from jl_ufw_unpack import jl_crypt  # noqa: E402

APP_AREA = 0x3000          # app area offset inside flash.bin
CHIP_KEY = 0xB50F
APP_BIN_OFFSET = 0x100     # app.bin inside the app area (runs at 0x01E00100)


def crc16(data, crc=0):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def app_area_crypt(data, start):
    """Encrypt or decrypt app-area bytes beginning at app-area offset `start` (a multiple of 32)."""
    assert start % 32 == 0
    out = bytearray()
    for s in range(0, len(data), 32):
        out += jl_crypt(data[s:s + 32], CHIP_KEY ^ (((start + s) >> 2) & 0xFFFF))
    return bytes(out)


def build(base, app):
    ufw = bytearray(base)

    # --- .ufw header and records
    hdr = bytearray(jl_crypt(ufw[:0x40]))
    if crc16(hdr[2:0x40]) != struct.unpack_from('<H', hdr, 0)[0]:
        raise SystemExit('base .ufw: header CRC mismatch (not a stock UV-5R Mini .ufw?)')
    if hdr[0x10:0x16] != b'AC695N':
        raise SystemExit('base .ufw: chip is %r, expected AC695N' % bytes(hdr[0x10:0x16]))
    nrec = struct.unpack_from('<H', hdr, 8)[0]
    recs = []
    for i in range(nrec):
        off = 0x40 + 0x50 * i
        r = bytearray(jl_crypt(ufw[off:off + 0x50]))
        name = bytes(r[0x40:]).split(b'\0')[0].decode('latin1')
        recs.append((off, r, name))
    flash_rec = next((x for x in recs if x[2] == 'flash.bin'), None)
    if not flash_rec:
        raise SystemExit('base .ufw has no flash.bin')
    _, frec, _ = flash_rec
    f_off, f_len = struct.unpack_from('<II', frec, 8)
    flash = bytearray(ufw[f_off:f_off + f_len])

    # --- app area
    area_plain = bytearray(app_area_crypt(bytes(flash[APP_AREA:]), 0))
    head = area_plain[0:32]
    ent = area_plain[32:64]
    if bytes(head[16:29]) != b'app_area_head' or bytes(ent[16:23]) != b'app.bin':
        raise SystemExit('base .ufw: unexpected app area layout')
    old_area_len = struct.unpack_from('<I', head, 8)[0]
    max_app = old_area_len - APP_BIN_OFFSET
    if len(app) > max_app:
        raise SystemExit('app is %d bytes, the slot holds %d' % (len(app), max_app))

    new_area_len = APP_BIN_OFFSET + len(app)
    # app bytes, then 0xFF up to the old end so everything after the slot stays where it was
    area_plain[APP_BIN_OFFSET:old_area_len] = app + b'\xFF' * (old_area_len - new_area_len)

    struct.pack_into('<I', ent, 8, len(app))
    struct.pack_into('<H', ent, 2, crc16(app))
    struct.pack_into('<H', ent, 0, crc16(ent[2:32]))
    area_plain[32:64] = ent

    struct.pack_into('<I', head, 8, new_area_len)
    area_plain[0:32] = head
    struct.pack_into('<H', head, 2, crc16(area_plain[0x20:new_area_len]))
    struct.pack_into('<H', head, 0, crc16(head[2:32]))
    area_plain[0:32] = head

    flash[APP_AREA:] = app_area_crypt(bytes(area_plain), 0)

    # --- .ufw records and header
    ufw[f_off:f_off + f_len] = flash
    struct.pack_into('<H', frec, 4, crc16(flash))
    off0 = flash_rec[0]
    ufw[off0:off0 + 0x50] = jl_crypt(bytes(frec))
    table_end = 0x40 + 0x50 * nrec
    struct.pack_into('<H', hdr, 2, crc16(ufw[0x40:table_end]))
    struct.pack_into('<H', hdr, 0, crc16(hdr[2:0x40]))
    ufw[:0x40] = jl_crypt(bytes(hdr))
    return bytes(ufw)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--base', required=True, help='stock .ufw to start from (not modified)')
    ap.add_argument('--app', required=True, help='raw app image linked at 0x01E00100')
    ap.add_argument('-o', '--out', required=True)
    a = ap.parse_args()
    base = open(a.base, 'rb').read()
    app = open(a.app, 'rb').read()
    out = build(base, app)
    if os.path.abspath(a.out) == os.path.abspath(a.base):
        raise SystemExit('refusing to overwrite the base .ufw')
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    open(a.out, 'wb').write(out)
    print('wrote %s (%d bytes, app %d bytes)' % (a.out, len(out), len(app)))


if __name__ == '__main__':
    main()
