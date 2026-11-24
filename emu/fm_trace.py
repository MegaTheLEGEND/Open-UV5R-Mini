#!/usr/bin/env python3
"""Boot the stock app in the emulator, then run its FM broadcast receiver's open routine (the `fm_inside` driver,
0x01E3D184) and, if asked, its tune routine, and print every SFR write and RAM-code / mask-ROM call they make, in order.

    python emu/fm_trace.py --isa E:/pi32v2-isa [--boot 3000000] [--tune 10000] [--lo 100000 --mode 0]

The emulator has no FM block: reads of 0x1F17xx return what was last written, so anything that waits on the FM
hardware (calibration loops, the ADC level) is not real. The write order and values are the facts wanted here.
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import uv5r_emu as E  # noqa: E402

FM_OPEN = 0x01E3D184
FM_TUNE = 0x01E3DE8E        # sets the frequency (0x198C0 + 104), 10 kHz units
FM_LO = 0x01E3CF56          # tunes the front end: r0 = kHz, r1 = mode 0/1/2 (IF offset 1875 / 2143 / 1500 kHz)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--isa', default=os.path.join(os.path.dirname(ROOT), 'pi32v2-isa'))
    ap.add_argument('--app', default=os.path.join(ROOT, 'build', 'stock', 'unpacked', 'app.bin'))
    ap.add_argument('--boot', type=int, default=3_000_000, help='steps of stock start-up first')
    ap.add_argument('--tune', type=int, default=0, help='then tune to this frequency (10 kHz units, e.g. 10000)')
    ap.add_argument('--lo', type=int, default=0, help='then tune the front end to this frequency (kHz, e.g. 100000)')
    ap.add_argument('--mode', type=int, default=0, help='front-end mode for --lo (0, 1, 2)')
    ap.add_argument('--limit', type=int, default=4_000_000, help='steps allowed for each call')
    a = ap.parse_args()
    m = E.Machine(a.isa, a.app, log=lambda s: None)
    try:
        while m.cpu.steps < a.boot:
            m.step()
    except Exception as e:                       # noqa: BLE001
        print('boot stopped at %d steps: %s' % (m.cpu.steps, e))
    log = []
    w = m.periph.write
    m.periph.write = lambda addr, d: (log.append('w %06x = %s  pc %x' % (addr, d[::-1].hex(), m.cpu.pc)), w(addr, d))[1]
    rc = m.rom.call
    m.rom.call = lambda c: (log.append('rom %s(%x, %x, %x) from %x' % (m.rom.name(c.pc), c.R[0], c.R[1], c.R[2],
                                                                        c.rets)), rc(c))[1]
    calls = [('open', FM_OPEN, [])]
    if a.tune:
        calls.append(('tune', FM_TUNE, [a.tune]))
    if a.lo:
        calls.append(('lo %d kHz mode %d' % (a.lo, a.mode), FM_LO, [a.lo, a.mode]))
    for name, fn, args in calls:
        if not name:
            continue
        log.append('--- %s' % name)
        start = m.cpu.steps
        try:
            m.call(fn, args, limit=a.limit, stack=0x1F000)
        except Exception as e:                   # noqa: BLE001
            log.append('stopped after %d steps: %r' % (m.cpu.steps - start, e))
    print('\n'.join(log))
    rd = lambda x: struct.unpack('<I', m.bus.read(x, 4))[0]
    print('FM state 0x198C0+320..+340: ' + ' '.join('%x' % rd(0x198C0 + o) for o in range(320, 344, 4)))


if __name__ == '__main__':
    main()
