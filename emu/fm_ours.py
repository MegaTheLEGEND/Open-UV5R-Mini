#!/usr/bin/env python3
"""Run OUR firmware's FM start (FMRX_Open) in the emulator and print every SFR write, in order, in the same form as
emu/fm_trace.py prints stock's, so the two can be compared line by line.

    python emu/fm_ours.py --isa ../pi32v2-isa [--mode 7] [--khz 100000] [--filter 1f17,1e19,1e00,1e2f,10f1]

The emulator has no FM, ADC or DAC analog blocks: done bits are forced on (ADC CON bit 7), everything else reads
back what was written. The order and the fixed values are what count, not calibration results.
"""
import argparse
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import uv5r_emu as E  # noqa: E402
from fm_cost import symbols, section  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--isa', default=os.path.join(os.path.dirname(ROOT), 'pi32v2-isa'))
    ap.add_argument('--elf', default=os.path.join(ROOT, 'build', 'fw', 'firmware.elf'))
    ap.add_argument('--bin', default=os.path.join(ROOT, 'build', 'fw', 'firmware.bin'))
    ap.add_argument('--tools', default='E:/toolchains/pi32v2-llvm/bin')
    ap.add_argument('--mode', type=int, default=7)
    ap.add_argument('--khz', type=int, default=100000)
    ap.add_argument('--limit', type=int, default=60_000_000)
    ap.add_argument('--filter', default='', help='comma list of address prefixes (hex) to print; default all')
    a = ap.parse_args()
    sym = symbols(os.path.join(a.tools, 'llvm-nm'), a.elf)
    oc = os.path.join(a.tools, 'llvm-objcopy')
    m = E.Machine(a.isa, a.bin, log=lambda s: None)
    lo, hi = sym['__ramfunc_start'], sym['__ramfunc_end']
    region = m.bus._region
    m.bus._region = lambda x: (m.bus.ram, x - 0x02000000) if lo <= x < hi else region(x)
    rom_call = m.rom.call
    m.rom.call = lambda c: c.step() if lo <= c.pc < hi else rom_call(c)
    tmp = os.path.join(ROOT, 'build', 'fm_ours.tmp')
    m.bus.write(lo - 0x02000000, section(oc, a.elf, '.ramfunc', tmp))
    m.bus.write(sym['__data_start'], section(oc, a.elf, '.data', tmp))
    os.remove(tmp)

    p = m.periph
    p.read_hooks[0x1E3100] = lambda base: p.regs[base] | 0x80          # SAR ADC: conversion always done
    filt = [f.lower() for f in a.filter.split(',') if f]
    log = []
    w = p.write

    def wr(addr, d):
        if not filt or any(('%06x' % addr).startswith(f) for f in filt):
            log.append('w %06x = %s  pc %x' % (addr, d[::-1].hex(), m.cpu.pc))
        return w(addr, d)
    p.write = wr
    try:
        r = m.call(sym['FMRX_Open'], [a.khz, a.mode], limit=a.limit, stack=0xD900)
        log.append('FMRX_Open returned %d after %d steps' % (r, m.cpu.steps))
    except Exception as e:                       # noqa: BLE001
        log.append('stopped after %d steps at pc %x: %r' % (m.cpu.steps, m.cpu.pc, e))
    print('\n'.join(log))


if __name__ == '__main__':
    main()
