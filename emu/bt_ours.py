#!/usr/bin/env python3
"""Run OUR firmware's Bluetooth start (BT_LeScanStart, src/platform/jl_sdk/bt_le.c) in the emulator and print every
SFR write in order, as emu/fm_ours.py does for FM, to compare with stock's Bluetooth start-up (build/bt_init.txt).
Then it runs the main loop's part (BTLE_Tick) a few times and BT_LeAdvStart, and prints how far each got.

    python emu/bt_ours.py --isa ../pi32v2-isa [--filter 1e19,1f18,1e00,10f1] [--adv] [--serial]

The emulator has no Bluetooth radio: registers read back what was written, and the hooks below force the few done
or lock bits the start waits on (each named with the register it fakes). So this checks that the start runs to the
end without a crash or a hang and writes what stock writes, not that the radio works.
"""
import argparse
import os
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
    ap.add_argument('--limit', type=int, default=40_000_000)
    ap.add_argument('--filter', default='', help='comma list of address prefixes (hex) to print; default all')
    ap.add_argument('--adv', action='store_true', help='also start advertising')
    ap.add_argument('--serial', action='store_true', help='also open the serial link (connectable advertising)')
    ap.add_argument('--quiet', action='store_true', help='only the summary')
    a = ap.parse_args()
    sym = symbols(os.path.join(a.tools, 'llvm-nm'), a.elf)
    oc = os.path.join(a.tools, 'llvm-objcopy')
    m = E.Machine(a.isa, a.bin, log=lambda s: None)
    lo, hi = sym['__ramfunc_start'], sym['__ramfunc_end']
    region = m.bus._region
    m.bus._region = lambda x: (m.bus.ram, x - 0x02000000) if lo <= x < hi else region(x)
    rom_call = m.rom.call
    m.rom.call = lambda c: c.step() if lo <= c.pc < hi else rom_call(c)
    tmp = os.path.join(ROOT, 'build', 'bt_ours.tmp')
    m.bus.write(lo - 0x02000000, section(oc, a.elf, '.ramfunc', tmp))
    m.bus.write(sym['__data_start'], section(oc, a.elf, '.data', tmp))
    os.remove(tmp)

    # JieLi's start-up calibrates with many delay(n * 60) busy loops: a millisecond or so each on the chip, minutes in
    # this emulator. Time means nothing here, so delay returns at once.
    m.observe[sym['delay']] = lambda c: setattr(c, 'pc', c.rets)
    # Our FLASH_Read (the pairing secret from the BTIF block) waits on the flash controller, which the emulator lacks:
    # it returns at once (the buffer keeps what it had). It runs from RAM, which takes the ROM path above.
    flash_read, ram_call = sym['FLASH_Read'], m.rom.call
    m.rom.call = lambda c: setattr(c, 'pc', c.rets) if c.pc == flash_read else ram_call(c)

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
    reads = []                                   # the last SFR reads: what a stuck loop polls
    rd = p.read

    def rdh(addr, n):
        reads.append((addr, m.cpu.pc))
        if len(reads) > 4000:
            del reads[:2000]
        return rd(addr, n)
    p.read = rdh

    def where(pc):
        best = min(((n, pc - v) for n, v in sym.items() if 0 <= pc - v < 0x4000), key=lambda t: t[1],
                   default=('?', 0))
        return '%s+%#x' % best

    def stuck_report():
        c = m.cpu
        log.append('  rets %x (%s)' % (c.rets, where(c.rets)))
        seen = []
        for k in range(0, 512, 4):               # return addresses on the stack, innermost first
            v = int.from_bytes(m.bus.read(c.sp + k, 4), 'little')
            if 0x01E00100 <= v < 0x01E80000 and v not in seen:
                seen.append(v)
                log.append('  stack %x (%s)' % (v, where(v)))
            if len(seen) >= 12:
                break
        tally = {}
        for addr, pc in reads[-2000:]:
            tally[(addr, pc)] = tally.get((addr, pc), 0) + 1
        for (addr, pc), n in sorted(tally.items(), key=lambda t: -t[1])[:8]:
            log.append('  read %06x x%d  pc %x (%s)' % (addr, n, pc, where(pc)))

    def call(name, args):
        try:
            r = m.call(sym[name], args, limit=a.limit, stack=0xD900)
            log.append('%s returned %d after %d steps' % (name, r, m.cpu.steps))
            return True
        except Exception as e:                   # noqa: BLE001
            near = min(((pc_name, m.cpu.pc - v) for pc_name, v in sym.items() if 0 <= m.cpu.pc - v < 0x4000),
                       key=lambda t: t[1], default=('?', 0))
            log.append('%s stopped after %d steps at pc %x (%s+%#x): %r' % (name, m.cpu.steps, m.cpu.pc,
                                                                          near[0], near[1], e))
            stuck_report()
            return False

    def step_of():
        return m.bus.read(sym['gBtLe'], 1)[0]

    ok = call('BT_LeScanStart', [1])
    log.append('start step %d' % step_of())
    def stats():
        b = m.bus.read(sym['gBtLe'], 20)
        u = lambda k: int.from_bytes(b[k:k + 4], 'little')
        q = int.from_bytes(m.bus.read(sym['gBtQPosts'], 4), 'little')
        log.append('  step %d last event %#x/%#x starts %d events %d msgs %d other %d queue posts %d'
                   % (b[0], b[1], b[2], u(4), u(8), u(12), u(16), q))
    stats()
    if ok:
        for _ in range(3):
            call('BTLE_Tick', [])
        stats()
        if a.adv:
            data = sym['__btram_heap_end'] - 64         # some RAM for the data: the top of the Bluetooth heap
            m.bus.write(data, bytes([2, 1, 6, 4, 9, ord('U'), ord('V'), ord('5')]))
            call('BT_LeAdvStart', [data, 8, 500])
            for _ in range(3):
                call('BTLE_Tick', [])
            stats()
        if a.serial:
            call('BT_Init', [sym['BTLE_IsOn']])          # any function: no phone connects, so it is never called
            for _ in range(3):
                call('BTLE_Tick', [])
            stats()
            call('BT_Init', [0])
        call('BT_LeScanStop', [])
        log.append('after stop: step %d' % step_of())
    out = [l for l in log if not a.quiet or not l.startswith('w ')]
    print('\n'.join(out))


if __name__ == '__main__':
    main()
