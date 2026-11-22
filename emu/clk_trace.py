#!/usr/bin/env python3
"""Boot the stock app in the emulator, then run what its update hand-over does to the clocks (clk_set("sys",
24 MHz), sub_01E69AAC, called from sub_01E43174) and print every SFR write and mask-ROM call it makes, in order.

    python emu/clk_trace.py --isa E:/pi32v2-isa [--boot 3000000] [--hz 24000000]

The writes depend on the clock the app was running at before (RAM 0x22664), printed first.
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import uv5r_emu as E  # noqa: E402

CLK_SET, SYS = 0x01E69AAC, 0x01E654CA


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--isa', default=os.path.join(os.path.dirname(ROOT), 'pi32v2-isa'))
    ap.add_argument('--app', default=os.path.join(ROOT, 'build', 'stock', 'unpacked', 'app.bin'))
    ap.add_argument('--boot', type=int, default=3_000_000, help='steps of stock start-up first')
    ap.add_argument('--hz', type=int, default=24_000_000)
    a = ap.parse_args()
    m = E.Machine(a.isa, a.app, log=lambda s: None)
    try:
        while m.cpu.steps < a.boot:
            m.step()
    except Exception as e:                       # noqa: BLE001
        print('boot stopped at %d steps: %s' % (m.cpu.steps, e))
    rd = lambda x: struct.unpack('<I', m.bus.read(x, 4))[0]
    print('before: sys clock [0x22664] = %d, [0xD060] = %#x' % (rd(0x22664), rd(0xD060)))
    log = []
    w = m.periph.write
    m.periph.write = lambda addr, d: (log.append('w %06x = %s  pc %x' % (addr, d[::-1].hex(), m.cpu.pc)), w(addr, d))[1]
    rc = m.rom.call
    m.rom.call = lambda c: (log.append('rom %s(%x, %x)' % (m.rom.name(c.pc), c.R[0], c.R[1])), rc(c))[1]
    m.call(CLK_SET, [SYS, a.hz], stack=0x1F000)
    print('\n'.join(log))
    print('after: sys clock [0x22664] = %d' % rd(0x22664))
    print('settings RAM 0xD04C..0xD080: ' + ' '.join('%x:%x' % (x, rd(x)) for x in range(0xD04C, 0xD080, 4)))


if __name__ == '__main__':
    main()
