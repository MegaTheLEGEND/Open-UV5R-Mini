#!/usr/bin/env python3
"""Boot the stock app in the emulator and list every write it makes, during start-up, to the registers JieLi's FM
receiver shares with the Bluetooth radio: the clock registers (0x1E0000..), the RF analog block JL_ANA / WLA_CON
(0x1E1900..) and the wireless block (0x1F1800..). Stock sets these up (its Bluetooth start-up, sub_01E0173E) before
FM is ever opened; our firmware has no Bluetooth start-up, so the FM receiver starts on whatever the bootloader left.

    python emu/wl_state.py --isa ../pi32v2-isa [--boot 3000000]

Prints the writes in order with the pc, then the final value of each register touched.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import uv5r_emu as E  # noqa: E402

RANGES = [(0x1E0000, 0x1E0080), (0x1E1900, 0x1E1A00), (0x1F1800, 0x1F1900)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--isa', default=os.path.join(os.path.dirname(ROOT), 'pi32v2-isa'))
    ap.add_argument('--app', default=os.path.join(ROOT, 'build', 'stock', 'unpacked', 'app.bin'))
    ap.add_argument('--boot', type=int, default=3_000_000)
    a = ap.parse_args()
    m = E.Machine(a.isa, a.app, log=lambda s: None)
    log = []
    w = m.periph.write

    def wr(addr, d):
        r = w(addr, d)
        base = addr & ~3
        if any(lo <= base < hi for lo, hi in RANGES):
            log.append('w %06x = %08x  pc %x' % (base, m.periph.regs[base], m.cpu.pc))
        return r
    m.periph.write = wr
    try:
        while m.cpu.steps < a.boot:
            m.step()
    except Exception as e:                       # noqa: BLE001
        log.append('boot stopped at %d steps: %s' % (m.cpu.steps, e))
    print('\n'.join(log))
    print('--- final values')
    for base in sorted(k for k in m.periph.regs if isinstance(k, int)):
        if any(lo <= base < hi for lo, hi in RANGES):
            print('%06x = %08x' % (base, m.periph.regs[base]))


if __name__ == '__main__':
    main()
