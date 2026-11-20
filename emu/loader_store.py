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
    # Instead of booting: what the bootloader leaves for the app. The decrypted app-area directory sits at the
    # start of the flash cache window, and boot info at RAM 0xD920 points at it (read by the mount at 0x01E18030).
    area = open(os.path.join(os.path.dirname(a.app), 'app_area.dec.bin'), 'rb').read()
    m.bus.flash[0:0x100] = area[0:0x100]
    m.bus.write(0xD920, bytes(32))
    m.bus.write(0xD920 + 16, struct.pack('<II', 0, E.FLASH_BASE))
    m.cpu.sp = 0xD900
    try:
        r = m.call(0x01E18030, [], stack=0xD900)
        print('flash mount returned %#x' % r)
        r = m.call(VM_INIT, [], stack=0xD900)
        print('VM init returned %#x' % r)
        print('VM state after init:', vm_state(m))
        nops = len(m.rom.flash_log)
        # as the closed library calls it at 0x01E691E8: r0 = VM length, r1 = loader length, r2 = VM address,
        # r3 = &out2; stack: &out (the chosen address), r6, type (UART_UPDATA)
        for cmd in (100, 101, 102, 103, 0x40045602):
            m.bus.write(0xD010, bytes(16))
            rr = m.call(IOCTL, [cmd, 0xD010], stack=0xD900)
            print('ioctl %#x -> %#x, out %s' % (cmd, rr, m.bus.read(0xD010, 16).hex()))
        OUT, OUT2 = 0xD000, 0xD004
        m.bus.write(OUT, bytes(8))
        r = m.call(ALLOC, [0x6A000, 15848, 0x94000, OUT2, OUT, 0, 0x5A04], stack=0xD900)
        addr, out2 = struct.unpack('<II', m.bus.read(OUT, 8))
        print('ALLOCATOR returned %#x: loader address %#x, out2 %#x' % (r, addr, out2))
        nor = m.bus.nor
        print('0xFA0F4:', nor[0xFA0F4:0xFA114].hex())
        print('0xFDF00:', nor[0xFDF00:0xFDF50].hex())
        print('flash ops by the allocator:', m.rom.flash_log[nops:nops + 40])
    except Exception as e:  # noqa: BLE001
        print('stopped:', e, 'pc %#x' % m.cpu.pc)
    print('boot: %d instructions, pc %#x' % (m.cpu.steps, m.cpu.pc))
    for what, at in seen[:20]:
        print('  %-36s at %d' % (what, at))
    print('VM state:', vm_state(m))
    print('flash ops:', m.rom.flash_log[:20], '... %d total' % len(m.rom.flash_log))


if __name__ == '__main__':
    main()
