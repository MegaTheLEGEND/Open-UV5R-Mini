#!/usr/bin/env python3
"""Run the stock update mode's cable-pin setup (the tail of sub_01E492C0) in the emulator and print what it does to
the SFRs, as set/clear masks, so src/driver/uart.c can do the same.

    python emu/uart_pins.py --isa E:/pi32v2-isa

The stock code configures the RX pin (USB D-) and then the TX pin (USB D+) with the GPIO helpers sub_01E001D2..
sub_01E003B8, then maps the UART onto them (sub_01E4856C for RX, sub_01E484B6 for TX). The same sequence is run
twice, once with every SFR starting at 0 and once at 0xFFFFFFFF: a bit that ends the same in both runs is set or
cleared by the code, any other bit is left as it was. The result does not depend on reset values.
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import uv5r_emu as E  # noqa: E402

PIN_TABLE = 0x01E65538            # {tx u16, rx u16, b4, b5}: the cable UART's pins (sub_01E492C0 reads it via r7)
RX_STEPS = [(0x01E001D2, 0), (0x01E00228, 0), (0x01E0027E, 1), (0x01E002E0, None), (0x01E00316, 1),
            (0x01E003B8, 0)]
TX_STEPS = [(0x01E001D2, 0), (0x01E00228, 1), (0x01E0027E, 1), (0x01E002E0, None), (0x01E00316, 0),
            (0x01E003B8, 1)]


def run(m, fill, table, stack):
    periph = m.periph
    periph.regs.clear()
    periph.regs.default_factory = lambda: fill
    tx, rx, b4, b5 = table
    for fn, a in RX_STEPS:
        m.call(fn, [rx] if a is None else [rx, a], stack=stack)
    for fn, a in TX_STEPS:
        m.call(fn, [tx] if a is None else [tx, a], stack=stack)
    m.call(0x01E4856C, [rx, b5], stack=stack)
    m.call(0x01E484B6, [tx, 1, b4], stack=stack)
    return dict(periph.regs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--isa', default=os.path.join(os.path.dirname(ROOT), 'pi32v2-isa'))
    ap.add_argument('--app', default=os.path.join(ROOT, 'build', 'stock', 'unpacked', 'app.bin'))
    a = ap.parse_args()
    m = E.Machine(a.isa, a.app, log=lambda s: None)
    table = struct.unpack('<HHBB', m.bus.read(PIN_TABLE, 6))
    print('pin table: tx %d rx %d b4 %#x b5 %#x' % table)
    stack = 0x1F000
    lo = run(m, 0, table, stack)
    hi = run(m, 0xFFFFFFFF, table, stack)
    for reg in sorted(set(lo) | set(hi)):
        if reg < 0x100000 or reg in (0x101044,):
            continue
        v0, v1 = lo.get(reg, 0), hi.get(reg, 0xFFFFFFFF)
        set_ = v0 & v1
        clr = ~v0 & ~v1 & 0xFFFFFFFF
        if set_ or clr:
            print('%06x  set %08x  clear %08x' % (reg, set_, clr))


if __name__ == '__main__':
    main()
