#!/usr/bin/env python3
"""Check lifted functions against the stock ones, on the calls the stock app really makes.

    python tools/lift/func_test.py [--steps 3000000] [--per-func 2] [--only sub_01E00228,...]

1. Record: the emulator (emu/uv5r_emu.py) boots the stock app. Each time a function is entered, a recording starts:
   the registers, then every memory read and write (byte by byte, with the instruction's pc) and every mask-ROM
   call (with the registers it returned), until the function returns.
2. Replay: the lifted C (build/lift, tools/lift/lift.py) runs from the same registers. Its memory reads are served
   from the recording and its writes are checked against it, in order (a small window allows for the order of
   accesses inside one instruction); ROM calls replay the recorded result. At the return, all registers and sp must
   agree, and no write the stock code made may be missing.
A function passes when it and everything it calls behave as the stock code did on those calls. The first
difference is reported with the pc of the stock instruction, so a failing function points at the line to fix.
"""
import argparse
import ctypes
import os
import subprocess
import sys
import time
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'emu'))
import lift as Lf  # noqa: E402
import uv5r_emu as E  # noqa: E402

M = 0xFFFFFFFF
MAX_EVENTS = 400000


class Rec:
    def __init__(self, name, cpu):
        self.name = name
        self.entry = (list(cpu.R), cpu.sp, cpu.rets, cpu.flags, cpu.icfg)
        self.ret_pc, self.ret_sp = cpu.rets, cpu.sp
        self.step0 = cpu.steps
        self.ev = []
        self.exit = None
        self.dead = False


class Tracer:
    """cpu.mem for the emulator's CPU: passes through to the bus and feeds the active recordings."""

    def __init__(self, bus, area_len):
        self.bus, self.active, self.cpu, self.pending = bus, [], None, []
        self.flash_end = E.FLASH_BASE + area_len

    def _skip(self, a):
        return E.FLASH_BASE <= a < self.flash_end or a >= E.ROM_BASE

    def read(self, a, n):
        data = self.bus.read(a, n)
        if self.active and not self._skip(a & M):
            pc, n = self.cpu.pc, self.cpu.steps
            for r in self.active:
                for i, b in enumerate(data):
                    r.ev.append(('r', (a + i) & M, b, pc, n))
        return data

    def write(self, a, data):
        if self.active and not self._skip(a & M) and not self.cpu.decode(self.cpu.pc)[0].id.startswith('add.sp_i'):
            # (the model of `sp += n` leaves its test stub's scribble in the new frame: not a real write)
            pc, n = self.cpu.pc, self.cpu.steps
            sfr = E.SFR_LO <= (a & M) < E.SFR_HI or 0x100000 <= (a & M) < 0x110000
            old = bytes(len(data)) if sfr else self.bus.read(a, len(data))
            for r in self.active:
                for i, b in enumerate(data):
                    if not sfr and b == old[i]:
                        # a byte the store leaves unchanged (the model writes whole merged words): the lifted
                        # code may or may not write it
                        r.ev.append(('s', (a + i) & M, b, pc, n))
                        continue
                    e = ['w', (a + i) & M, b, pc, n, None if sfr else old[i], self.cpu.sp]
                    r.ev.append(e)
                    self.pending.append(e)
        self.bus.write(a, data)


def record(isa, app, steps, per_func, only, funcs_by_addr, log):
    m = E.Machine(isa, app, log=lambda s: None)
    cpu, rom = m.cpu, m.rom
    area_len = len(m.bus.flash)
    tr = Tracer(m.bus, area_len)
    tr.cpu = cpu
    cpu.mem = tr
    started = Counter()
    done = []
    active = tr.active
    t0 = time.time()
    err = None
    try:
        while cpu.steps < steps:
            pc = cpu.pc
            name = funcs_by_addr.get(pc)
            if name is not None and started[name] < per_func and (not only or name in only):
                started[name] += 1
                active.append(Rec(name, cpu))
            if pc >= E.ROM_BASE:
                before, n = (list(cpu.R), cpu.sp), cpu.steps
                rom.call(cpu)
                ev = ('rom', pc, (list(cpu.R), cpu.sp, cpu.rets, cpu.flags), before, n)
                for r in active:
                    r.ev.append(ev)
            else:
                ob = m.observe.get(pc)
                if ob:
                    ob(cpu)
                cpu.step()
            if tr.pending:
                # a write below the stack pointer (before and after the instruction) is dead memory: the ISA
                # models leave their test stub's scribbles there
                for e in tr.pending:
                    e[6] = min(e[6], cpu.sp)
                tr.pending.clear()
            if active:
                for r in list(active):
                    if cpu.pc == r.ret_pc and cpu.sp == r.ret_sp:
                        r.exit = (list(cpu.R), cpu.sp, cpu.rets)
                        active.remove(r)
                        done.append(r)
                    elif len(r.ev) > MAX_EVENTS:
                        r.dead = True
                        active.remove(r)
    except Exception as e:  # noqa: BLE001
        err = e
    log('recorded %d calls of %d functions in %d steps, %.0f s%s' % (
        len(done), len({r.name for r in done}), cpu.steps, time.time() - t0, '; emulator stopped: %r' % err if err
        else ''))
    return done, m


class Replay:
    def __init__(self, lib, area, verbose=False):
        self.lib, self.area, self.verbose = lib, area, verbose
        RD = ctypes.CFUNCTYPE(ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32)
        WR = ctypes.CFUNCTYPE(None, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32)
        ROMF = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_uint32)
        self._cbs = (RD(self.rd), WR(self.wr), ROMF(self.rom))
        lib.lift_set_callbacks(*self._cbs)

        class State(ctypes.Structure):
            _fields_ = [('r', ctypes.c_uint32 * 16), ('sp', ctypes.c_uint32), ('rets', ctypes.c_uint32),
                        ('flags', ctypes.c_uint32), ('icfg', ctypes.c_uint32), ('sr', ctypes.c_uint32 * 16)]
        lib.lift_state.restype = ctypes.POINTER(State)
        lib.lift_fault_why.restype = ctypes.c_char_p
        self.S = lib.lift_state().contents
        self.pc = ctypes.c_uint32.in_dll(lib, 'lift_pc')
        self.icount = ctypes.c_uint32.in_dll(lib, 'lift_icount')

    def fail(self, msg):
        if self.err is None:
            self.err = msg
            if self.verbose:
                step = self.step0 + self.icount.value - 1
                for k, e in enumerate(self.ev):
                    if step - 3 <= e[4] <= step + 3:
                        print('   %s %5d step %d %s' % ('*' if self.used[k] else ' ', k, e[4] - self.step0 + 1, fmt(e)))
                print('   lifted pc %#x' % self.pc.value)
        ctypes.c_int.in_dll(self.lib, 'lift_stop').value = 1

    def _find(self, kind, addr, val=None):
        """The first unused event of `kind` for addr made by the stock instruction execution that the lifted code
        is at (lockstep: the n-th instruction the lifted code runs is the n-th the stock code ran)."""
        step = self.step0 + self.icount.value - 1
        for k in self.by_step.get(step, ()):
            e = self.ev[k]
            if not self.used[k] and e[0] in kind and (addr is None or e[1] == addr) and                     (e[0] != 's' or e[2] == val):      # (an unchanged neighbour of a byte store: same value only)
                self.used[k] = True
                return e
        return None

    def here(self):
        step = self.step0 + self.icount.value - 1
        stock = self.by_step.get(step)
        pcs = sorted({self.ev[k][3] for k in stock}) if stock else []
        return 'lifted pc %#x, step %d; stock events at this step: %s' % (
            self.pc.value, self.icount.value, ' '.join('%#x' % x for x in pcs) or 'none (the stock code made no '
            'memory access there: control flow differs earlier)')

    def byte_rd(self, a):
        e = self._find(('r',), a)
        if e is not None:
            self.shadow[a] = e[2]
            return e[2]
        self.fail('read of %#x the stock code did not make (%s)' % (a, self.here()))
        return 0

    def rd(self, a, n):
        try:
            if E.FLASH_BASE <= a < E.FLASH_BASE + len(self.area):
                o = a - E.FLASH_BASE
                return int.from_bytes(self.area[o:o + n], 'little')
            v = 0
            for i in range(n):
                v |= self.byte_rd((a + i) & M) << (8 * i)
            return v
        except Exception as ex:  # noqa: BLE001
            self.fail('python: %r' % ex)
            return 0

    def wr(self, a, n, v):
        for i in range(n):
            ad, b = (a + i) & M, (v >> (8 * i)) & 0xFF
            e = self._find(('w', 's'), ad, b)
            if e is not None and e[0] == 's':
                self.shadow[ad] = b
                continue
            if e is None:
                # the ISA model drops stores that leave memory unchanged (it read the word to merge it)
                r = self._find(('r',), ad)
                if (r is not None and r[2] == b) or (r is None and self.shadow.get(ad) == b):
                    self.shadow[ad] = b
                    continue
            if e is None:
                self.fail('write %#x = %02x the stock code did not make (%s)' % (ad, b, self.here()))
                return
            if e[2] != b:
                self.fail('write %#x = %02x, stock wrote %02x (stock pc %#x)' % (ad, b, e[2], e[3]))
                return
            self.shadow[ad] = b

    def rom(self, addr):
        e = self._find(('rom',), None)
        if e is None or e[1] != addr:
            self.fail('ROM call %#x, stock: %s (%s)' % (addr, fmt(e), self.here()))
            return 0
        regs, sp, rets, flags = e[2]
        for i in range(16):
            self.S.r[i] = regs[i]
        self.S.sp, self.S.rets, self.S.flags = sp, rets, flags
        return 0

    def run(self, rec, addr):
        self.ev, self.used, self.shadow, self.err = rec.ev, bytearray(len(rec.ev)), {}, None
        self.step0 = rec.step0
        self.by_step = defaultdict(list)
        for k, e in enumerate(rec.ev):
            self.by_step[e[4] if e[0] != 'rom' else e[4]].append(k)
        R, sp, rets, flags, icfg = rec.entry
        S = self.S
        for i in range(16):
            S.r[i] = R[i]
        S.sp, S.rets, S.flags, S.icfg = sp, rets, flags, icfg
        rc = self.lib.lift_run(addr)
        if rc:
            if self.err:
                return self.err
            return 'stopped at %#x: %s' % (self.lib.lift_fault_pc(), self.lib.lift_fault_why().decode())
        xR, xsp, xrets = rec.exit
        diff = ['r%d %08x/%08x' % (i, S.r[i], xR[i]) for i in range(16) if S.r[i] != xR[i]]
        if S.sp != xsp:
            diff.append('sp %08x/%08x' % (S.sp, xsp))
        if diff:
            return 'registers at return [lifted/stock]: ' + ' '.join(diff)
        for k, e in enumerate(rec.ev):
            if not self.used[k] and e[0] == 'w' and e[5] is not None and e[2] != e[5] and e[1] >= e[6]:
                return 'missing write %#x = %02x (stock pc %#x)' % (e[1], e[2], e[3])
            if not self.used[k] and e[0] == 'rom':
                return 'missing ROM call %#x' % e[1]
        return None


def fmt(e):
    if e is None:
        return 'none'
    if e[0] == 'rom':
        return 'ROM call %#x' % e[1]
    return '%s %#x=%02x at pc %#x' % (e[0], e[1], e[2], e[3])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--isa', default=os.path.join(os.path.dirname(ROOT), 'pi32v2-isa'))
    ap.add_argument('--app', default=os.path.join(ROOT, 'build', 'stock', 'unpacked', 'app.bin'))
    ap.add_argument('--dis', default=os.path.join(ROOT, 'build', 'app.dis'))
    ap.add_argument('--lift', default=os.path.join(ROOT, 'build', 'lift'))
    ap.add_argument('--steps', type=int, default=3000000)
    ap.add_argument('--per-func', type=int, default=2)
    ap.add_argument('--only', default='')
    ap.add_argument('--verbose', action='store_true', help='print the recorded events around each failure')
    ap.add_argument('--no-build', action='store_true', help='use the existing build/lift/lifted.dll')
    a = ap.parse_args()
    dll = os.path.join(a.lift, 'lifted.dll')
    if not a.no_build:
        subprocess.run([sys.executable, os.path.join(HERE, 'lift.py'), '--isa', a.isa, '--app', a.app, '--dis',
                        a.dis, '--out', a.lift], check=True)
        subprocess.run(['gcc', '-O1', '-w', '-shared', '-I', a.lift, os.path.join(a.lift, 'lifted.c'),
                        os.path.join(HERE, 'runtime.c'), '-o', dll], check=True)
    lib = ctypes.CDLL(dll)
    funcs = Lf.read_functions(a.dis)
    by_addr = {start: name for name, start, _ in funcs}
    only = set(x for x in a.only.split(',') if x)
    recs, m = record(a.isa, a.app, a.steps, a.per_func, only, by_addr, print)
    rp = Replay(lib, bytes(m.bus.flash), a.verbose)
    results = defaultdict(list)
    for r in recs:
        results[r.name].append(rp.run(r, int(r.name[4:], 16)))
    ok = [n for n, v in results.items() if all(x is None for x in v)]
    bad = {n: next(x for x in v if x) for n, v in results.items() if any(v)}
    for n in sorted(bad):
        print('%-14s %s' % (n, bad[n][:300]))
    reasons = Counter()
    for v in bad.values():
        reasons[v.split(':')[0].split(' (')[0][:50]] += 1
    print('\nfailure kinds:', ', '.join('%s: %d' % kv for kv in reasons.most_common(12)))
    print('%d functions exercised: %d match the stock code on every recorded call, %d differ' % (
        len(results), len(ok), len(bad)))


if __name__ == '__main__':
    main()
