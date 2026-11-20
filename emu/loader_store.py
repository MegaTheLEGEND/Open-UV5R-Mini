#!/usr/bin/env python3
"""Run the stock app's loader-store address choice (sub_01E42AF2) in the emulator, to learn its rule.

    python emu/loader_store.py --isa E:/pi32v2-isa [--boot-steps 9000000]

The internal flash is modelled behind the ROM flash routines (emu/uv5r_emu.py, Rom): stock flash.bin at 0x1000, the
VM settings area erased. The app boots far enough to set up its flash driver and settings store; then the
allocator is called the way the closed update library calls it, and every flash read, erase and write is logged.
docs/UPDATE-MODE.md has what this is for.
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import uv5r_emu as E  # noqa: E402
from ufwcheck import flash_of  # noqa: E402

VM_INIT = 0x01E56952
IOCTL = 0x01E42264
ALLOC = 0x01E42AF2
B = 0x198C0


def nor_image(stock):
    nor = bytearray(b'\xFF' * 0x100000)
    fl = flash_of(stock)
    nor[0x1000:0x1000 + len(fl)] = fl
    return nor


def vm_state(m):
    rd = lambda off: struct.unpack('<I', m.bus.read(B + off, 4))[0]   # noqa: E731
    return {'bank0': hex(rd(1523 * 4)), 'bank0+4': hex(rd(1523 * 4 + 4)), 'bank0+8': hex(rd(1523 * 4 + 8)),
            'bank1': hex(rd(1526 * 4)), 'bank1+4': hex(rd(1526 * 4 + 4)), 'bank1+8': hex(rd(1526 * 4 + 8)),
            'w1677': hex(rd(1677 * 4)), 'sel6713': hex(m.bus.read(B + 6713, 1)[0]),
            'info1416': m.bus.read(B + 1416, 16).hex(), 'lim352': hex(rd(352 * 4)), 'lim358': hex(rd(358 * 4))}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--isa', default=os.path.join(os.path.dirname(ROOT), 'pi32v2-isa'))
    ap.add_argument('--app', default=os.path.join(ROOT, 'build', 'stock', 'unpacked', 'app.bin'))
    ap.add_argument('--stock', default=os.path.join(ROOT, 'firmware', 'stock-nrf-v0.27.ufw'))
    ap.add_argument('--boot-steps', type=int, default=9_000_000)
    a = ap.parse_args()
    stock = open(a.stock, 'rb').read()
    seen = []
    m = E.Machine(a.isa, a.app, nor=nor_image(stock), log=lambda s: None)
    m.observe[VM_INIT] = lambda c: seen.append(('vm_init', c.steps))
    m.observe[IOCTL] = lambda c: seen.append(('ioctl %#x(%#x)' % (c.R[0], c.R[1]), c.steps)) \
        if c.R[0] in (0x40045601, 0x40045602) else None
    try:
        while m.cpu.steps < a.boot_steps:
            m.step()
    except Exception as e:  # noqa: BLE001
        print('boot stopped:', e)
    print('boot: %d instructions, pc %#x' % (m.cpu.steps, m.cpu.pc))
    for what, at in seen[:20]:
        print('  %-36s at %d' % (what, at))
    print('VM state:', vm_state(m))
    print('flash ops:', m.rom.flash_log[:20], '... %d total' % len(m.rom.flash_log))


if __name__ == '__main__':
    main()
