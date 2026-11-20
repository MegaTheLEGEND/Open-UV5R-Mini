#!/usr/bin/env python3
"""Check the lifter's C for every instruction form in the stock app against the ISA's models.

    python tools/lift/insn_test.py [--per-form 12] [--states 16] [--only ld.w,st.b]

For each instruction form (insn id) up to --per-form instances are taken from the app. Each is lifted to C
(tools/lift/lift.py), all of them compiled into one DLL, and run on --states random register/memory states; the
same states go through the pi32v2-isa model of the instruction (the models fitted to real hardware). Registers,
sp, rets, written memory and branch decisions must agree. Prints one line per form that disagrees, then a summary.
"""
import argparse
import ctypes
import os
import random
import struct
import subprocess
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'emu'))
import lift as Lf  # noqa: E402

M = 0xFFFFFFFF
# Forms whose effect the emulator defines (special registers, interrupts, sync, hints): no ISA model to check
HOST_DEFINED = {'li.sr_u32', 'mov.sr_r', 'mov.r_sr', 'push.srl', 'pop.srl', 'push.srmask', 'pop.srmask', 'cli',
                'cli.r', 'sti', 'sti.r', 'lockset', 'lockclr', 'ssync', 'csync', 'idle', 'sevl', 'wfe', 'btbclr',
                'bkpt', 'hbkpt', 'nop'}


class SparseMem:
    """Every address reads a fixed pseudo-random byte until written; the flash window reads the app."""

    def __init__(self, area, seed):
        self.area, self.seed, self.w = area, seed, {}

    def byte(self, a):
        a &= M
        if a in self.w:
            return self.w[a]
        if Lf.FLASH_BASE <= a < Lf.FLASH_BASE + len(self.area):
            return self.area[a - Lf.FLASH_BASE]
        x = (a * 2654435761 + self.seed * 40503) & M
        return (x >> 13) & 0xFF

    def read(self, a, n):
        return bytes(self.byte(a + i) for i in range(n))

    def write(self, a, data):
        for i, b in enumerate(data):
            self.w[(a + i) & M] = b


def rand_word(rng):
    k = rng.randrange(6)
    if k == 0:
        return rng.choice([0, 1, 2, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0xFFFF, 0x8000, 0xFF, 0x80, 31, 32, 33])
    if k == 1:
        return rng.randrange(64)
    if k == 2:
        return rng.randrange(0x10000)
    if k == 3:
        return (-rng.randrange(1, 300)) & M
    return rng.getrandbits(32)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--isa', default=os.path.join(os.path.dirname(ROOT), 'pi32v2-isa'))
    ap.add_argument('--app', default=os.path.join(ROOT, 'build', 'stock', 'unpacked', 'app.bin'))
    ap.add_argument('--dis', default=os.path.join(ROOT, 'build', 'app.dis'))
    ap.add_argument('--per-form', type=int, default=12)
    ap.add_argument('--states', type=int, default=16)
    ap.add_argument('--only', default='')
    ap.add_argument('--verbose', action='store_true')
    a = ap.parse_args()
    L = Lf.Lifter(a.isa, a.app)
    import uv5r_emu as E
    sim = E.load_core(a.isa)
    only = set(x for x in a.only.split(',') if x)

    # 1. samples: up to per-form units per instruction form
    forms = defaultdict(list)
    for name, start, addrs in Lf.read_functions(a.dis):
        for pc in addrs:
            d = L.decode(pc)
            if d is None:
                continue
            fid = d[0].id
            if only and fid not in only:
                continue
            if len(forms[fid]) >= a.per_form:
                continue
            if fid.endswith('.p'):
                d2 = L.decode(pc + d[2])
                if d2 is None or d2[0].id in ('rep', 'rep.i') or d2[0].block:
                    continue
            try:
                u = L.unit_at(pc)
            except Lf.Unimpl:
                continue
            if u.kind in ('rep', 'call', 'callr', 'goto', 'gotor', 'tbb', 'unimpl'):
                continue
            if fid in HOST_DEFINED or any(x in HOST_DEFINED for x in u.text.split(' | ')[-1].split()[:1]):
                continue
            forms[fid].append(u)
    samples = [(fid, u) for fid in sorted(forms) for u in forms[fid]]
    L.save()

    # 2. one C function per sample
    out = os.path.join(ROOT, 'build', 'lift')
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, 'lift.h'), 'w') as f:
        f.write(Lf.RUNTIME_H)
    src = ['#include "lift.h"', '__declspec(dllexport) int lift_taken;',
           'void lift_dispatch(uint32_t addr, int *found) { (void)addr; *found = 0; }']
    for k, (fid, u) in enumerate(samples):
        body = u.code
        if u.kind in ('br', 'if'):
            body += ' lift_taken = (%s) ? 1 : 0;' % u.cond
        src.append('__declspec(dllexport) void t_%d(void) { uint32_t _a = 0, _rc = 0, _jt = 0, _pt = 0; int _pc = 0;'
                   ' (void)_a; (void)_rc; (void)_jt; (void)_pt; (void)_pc; %s }' % (k, body))
    cpath = os.path.join(out, 'insn_test.c')
    open(cpath, 'w').write('\n'.join(src) + '\n')
    dll = os.path.join(out, 'insn_test.dll')
    subprocess.run(['gcc', '-O0', '-w', '-shared', '-I', out, cpath, os.path.join(HERE, 'runtime.c'), '-o', dll],
                   check=True)
    lib = ctypes.CDLL(dll)

    class State(ctypes.Structure):
        _fields_ = [('r', ctypes.c_uint32 * 16), ('sp', ctypes.c_uint32), ('rets', ctypes.c_uint32),
                    ('flags', ctypes.c_uint32), ('icfg', ctypes.c_uint32), ('sr', ctypes.c_uint32 * 16)]
    lib.lift_state.restype = ctypes.POINTER(State)
    S = lib.lift_state().contents
    taken = ctypes.c_int.in_dll(lib, 'lift_taken')
    RD = ctypes.CFUNCTYPE(ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32)
    WR = ctypes.CFUNCTYPE(None, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32)
    ROMF = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_uint32)
    cmem = [None]
    rd = RD(lambda ad, n: int.from_bytes(cmem[0].read(ad, n), 'little'))
    wr = WR(lambda ad, n, v: cmem[0].write(ad, (v & M).to_bytes(4, 'little')[:n]))
    rom = ROMF(lambda ad: 1)
    lib.lift_set_callbacks(rd, wr, rom)

    cpu = E.make_core(sim)
    rng = random.Random(1)
    bad_forms, no_model, checked = {}, {}, defaultdict(int)
    for k, (fid, u) in enumerate(samples):
        fn = getattr(lib, 't_%d' % k)
        insn, raw, size = L.decode(u.pc)[:3]
        for s in range(a.states):
            seed = rng.getrandbits(30)
            R = [rand_word(rng) for _ in range(16)]
            sp = 0x8000 + 4 * rng.randrange(0x1000)
            rets = rng.getrandbits(32) & 0x07FFFFFE
            # model
            mm = SparseMem(L.area, seed)
            cpu.mem = mm
            cpu.R = list(R)
            cpu.sp, cpu.rets, cpu.flags, cpu.pc = sp, rets, 0, u.pc
            try:
                if insn.id.endswith('.p'):
                    res, _, _, _, _ = cpu.pair(insn, raw, size)
                    mtaken = None
                else:
                    res, op, tgt = cpu.effect(insn, raw, u.pc, size, cpu.R)
                    mtaken = res.get('exit') if u.kind == 'br' else None
                    if u.kind == 'if':
                        mtaken = res.get('regs', {}).get(8) == 0x100
                        res = dict(res)
                        res['regs'] = {}
            except Exception as e:  # noqa: BLE001  (a model that needs hardware context)
                no_model.setdefault(fid, str(e))
                break
            exp = list(R)
            for r, v in res.get('regs', {}).items():
                if r < 16:
                    exp[r] = v & M
            for ad, v in res.get('mem', {}).items():
                ad = int(ad, 16) if isinstance(ad, str) else ad
                mm.write(ad, struct.pack('<I', v & M))
            esp = res.get('sp', sp) & M
            erets = res.get('rets', rets) & M
            # lifted C
            cm = SparseMem(L.area, seed)
            cmem[0] = cm
            for i in range(16):
                S.r[i] = R[i]
            S.sp, S.rets, S.flags = sp, rets, 0
            taken.value = -1
            fn()
            got = list(S.r)
            why = []
            if got != exp:
                why.append('regs ' + ' '.join('r%d %08x/%08x' % (i, got[i], exp[i]) for i in range(16)
                                              if got[i] != exp[i]))
            if S.sp != esp:
                why.append('sp %08x/%08x' % (S.sp, esp))
            if u.kind != 'ret' and S.rets != erets:
                why.append('rets %08x/%08x' % (S.rets, erets))
            addrs = set(cm.w) | set(mm.w)
            # the models leave the test stub's scribbles below sp (and in a frame `sp += -n` just opened): dead
            low, high = min(sp, esp), (sp if fid.startswith('add.sp_i') else min(sp, esp))
            mem_bad = [ad for ad in sorted(addrs) if cm.byte(ad) != mm.byte(ad) and not (low - 64 <= ad < high)]
            if mem_bad:
                why.append('mem %s' % ' '.join('%x:%02x/%02x' % (ad, cm.byte(ad), mm.byte(ad)) for ad in mem_bad[:4]))
            if mtaken is not None and bool(taken.value) != bool(mtaken):
                why.append('taken %d/%d' % (taken.value, bool(mtaken)))
            checked[fid] += 1
            if why:
                ins = ' '.join('r%d=%08x' % (i, R[i]) for i in range(16) if ('S.r[%d]' % i) in u.code + str(u.cond))
                bad_forms.setdefault(fid, '%s @%x: %s   [C/model]  in: %s sp=%x' % (u.text, u.pc, '; '.join(why),
                                                                                   ins, sp))
                if not a.verbose:
                    break
    for fid, msg in sorted(bad_forms.items()):
        print('%-28s %s' % (fid, msg[:400]))
    ok = [f for f in forms if f not in bad_forms and f not in no_model]
    print('%d forms: %d agree with the model, %d disagree, %d without a usable model here (%d instances, '
          '%d states)' % (len(forms), len(ok), len(bad_forms), len(no_model), len(samples), sum(checked.values())))
    sys.exit(1 if bad_forms else 0)


if __name__ == '__main__':
    main()
