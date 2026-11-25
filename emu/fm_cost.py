#!/usr/bin/env python3
"""How much CPU one FM interrupt costs in OUR firmware: loads build/fw/firmware.elf into the emulator, puts its RAM
code where start.S would, and runs JieLi's FM demodulator (fm_dem, the bulk of interrupt 43) and our sample handler
(fm_sample_output_handler) on one half buffer, counting instructions.

    python emu/fm_cost.py --isa ../pi32v2-isa [--elf build/fw/firmware.elf] [--rate 1172]

The demodulator's work does not depend on the samples (fixed filter lengths), so zero samples and a zeroed state
give the real instruction count. Instructions are not cycles (loads, multiplies and flash waits take longer), so
the result is a lower bound on the CPU time the interrupt needs.
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


def symbols(nm, elf):
    out = subprocess.run([nm, elf], capture_output=True, text=True, check=True).stdout
    s = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3:
            s[p[2]] = int(p[0], 16)
    return s


def section(objcopy, elf, name, tmp):
    subprocess.run([objcopy, '-O', 'binary', '--only-section=' + name, elf, tmp], check=True)
    return open(tmp, 'rb').read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--isa', default=os.path.join(os.path.dirname(ROOT), 'pi32v2-isa'))
    ap.add_argument('--elf', default=os.path.join(ROOT, 'build', 'fw', 'firmware.elf'))
    ap.add_argument('--bin', default=os.path.join(ROOT, 'build', 'fw', 'firmware.bin'))
    ap.add_argument('--tools', default='E:/toolchains/pi32v2-llvm/bin')
    ap.add_argument('--rate', type=float, default=1172.0, help='FM interrupts a second')
    a = ap.parse_args()
    nm = os.path.join(a.tools, 'llvm-nm')
    oc = os.path.join(a.tools, 'llvm-objcopy')
    sym = symbols(nm, a.elf)
    m = E.Machine(a.isa, a.bin, log=lambda s: None)
    # RAM code runs at 0x02000000 + its RAM address (the instruction alias); the emulator treats all of 0x02000000..
    # as mask ROM, so map our .ramfunc range to RAM and run it as code
    lo, hi = sym['__ramfunc_start'], sym['__ramfunc_end']
    region = m.bus._region
    m.bus._region = lambda x: (m.bus.ram, x - 0x02000000) if lo <= x < hi else region(x)
    rom_call = m.rom.call
    m.rom.call = lambda c: c.step() if lo <= c.pc < hi else rom_call(c)
    tmp = os.path.join(ROOT, 'build', 'fm_cost.tmp')
    ram = section(oc, a.elf, '.ramfunc', tmp)
    m.bus.write(sym['__ramfunc_start'] - 0x02000000, ram)
    data = section(oc, a.elf, '.data', tmp)
    m.bus.write(sym['__data_start'], data)
    os.remove(tmp)

    w32 = lambda addr, v: m.bus.write(addr, struct.pack('<I', v))
    state = 0x22000 + 0x4000                     # a zeroed 6040-byte state block in RAM2 (heap area)
    m.bus.write(state, bytes(6040))
    w32(sym['fm_dem_buf_t'], state)
    w32(sym['fm_adc_dat_t'], sym['fm_adc_dat'])
    m.bus.write(sym['fm_adc_dat'], bytes(4096))

    s0 = m.cpu.steps
    m.call(sym['fm_dem'], [sym['fm_adc_dat'], state, sym['fm_out']], limit=5_000_000, stack=0xD900)
    dem = m.cpu.steps - s0
    s0 = m.cpu.steps
    m.call(sym['fm_sample_output_handler'], [sym['fm_out'], 128], limit=5_000_000, stack=0xD900)
    out = m.cpu.steps - s0
    tot = dem + out
    print('fm_dem: %d instructions, our handler: %d, total %d per interrupt' % (dem, out, tot))
    print('at %.0f interrupts a second: %.1f million instructions a second' % (a.rate, tot * a.rate / 1e6))


if __name__ == '__main__':
    main()
