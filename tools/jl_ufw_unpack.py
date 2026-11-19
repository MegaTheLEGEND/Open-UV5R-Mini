#!/usr/bin/env python3
"""Unpack a JieLi .ufw firmware (Baofeng UV-5R Mini, AC695N) into plain images.

usage: python jl_ufw_unpack.py stock-nrf-v0.27.ufw outdir
"""
import os, struct, sys

def jl_crypt(buf, key=0xFFFF):
    """JieLi 'ENC' cipher: XOR with a CRC16-CCITT LFSR keystream."""
    out = bytearray()
    for b in buf:
        out.append(b ^ (key & 0xFF))
        key = ((key << 1) ^ (0x1021 if key & 0x8000 else 0)) & 0xFFFF
    return bytes(out)

def entries(blob, off, count):
    for i in range(count):
        e = jl_crypt(blob[off + i*32: off + i*32 + 32])
        typ, _, crc, eoff, elen, attr = struct.unpack_from('<BBHIII', e)
        name = e[16:].split(b'\0')[0].split(b'\xff')[0].decode('latin1')
        if not name or not name.isprintable():
            break
        yield name, typ, crc, eoff, elen, attr

def main(path, outdir, chipkey=0xB50F):
    os.makedirs(outdir, exist_ok=True)
    ufw = open(path, 'rb').read()
    hdr = jl_crypt(ufw[:32])
    print('ufw chip id:', hdr[16:].split(b'\0')[0].decode())
    files = {}
    # ufw file records are 0x50 bytes each (starting at 0x40), each its own ENC block
    off = 0x40
    while off + 0x50 <= len(ufw):
        r = jl_crypt(ufw[off:off + 0x50])
        _, _, foff, flen = struct.unpack_from('<HHII', r, 4)
        name = r[0x40:].split(b'\0')[0].decode('latin1')
        if not name or not name.isprintable():
            break
        print(f'  ufw file {name:12s} @0x{foff:06x} len 0x{flen:x}')
        files[name] = ufw[foff:foff + flen]
        open(os.path.join(outdir, name), 'wb').write(files[name])
        off += 0x50
    flash = files['flash.bin']
    for name, typ, crc, off, ln, _ in entries(flash, 32, 8):
        print(f'  flash entry {name:16s} @0x{off:06x} len 0x{ln:x}')
        if name == 'uboot.boot':
            open(os.path.join(outdir, 'uboot.boot'), 'wb').write(flash[off:off + ln])
        if name == 'app_dir_head':
            app_base = off
    # app area: per 32-byte block, key = chipkey ^ ((addr - app_base) >> 2)
    app = bytearray()
    for s in range(app_base, len(flash), 32):
        app += jl_crypt(flash[s:s + 32], chipkey ^ (((s - app_base) >> 2) & 0xFFFF))
    open(os.path.join(outdir, 'app_area.dec.bin'), 'wb').write(app)
    for i in range(8):
        e = app[i*32:i*32+32]
        _, _, eoff, elen, _ = struct.unpack_from('<HHIII', e)
        name = bytes(e[16:28]).split(b'\0')[0].split(b'\xff')[0].decode('latin1')
        if not name or not name.isprintable():
            continue
        print(f'  app entry {name:14s} @0x{eoff:06x} len 0x{elen:x}')
        if name == 'app.bin':
            open(os.path.join(outdir, 'app.bin'), 'wb').write(app[eoff:eoff + elen])

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else 'out')
