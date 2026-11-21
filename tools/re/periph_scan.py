#!/usr/bin/env python3
"""List, for every function in an app.dis listing, the GPIO pins and peripheral registers it touches.

usage: python tools/re/periph_scan.py build/app.dis [--pins] [--sfr LO-HI] [--func sub_XXXXXXXX]

Pins: a constant loaded into r0 shortly before a call to a function that takes a pin number (GPIO_PortFromPin
sub_01E0019C, or any function that passes its r0 straight on to it). SFRs: any 32-bit constant in 0x1E0000..0x1FFFFF
(the AC695N peripheral space) or 0x100000..0x10FFFF.
"""
import argparse
import collections
import re

LINE = re.compile(r'^([0-9a-f]{8}):\s+(?:[0-9a-f]{2} )+\s*(.*)$')
CONST = re.compile(r'= (-?\d+)\s*(?:;|$)')
CALL = re.compile(r'call .*?(sub_[0-9A-F]{8})')


def pname(p):
    return 'P%s%d' % ('ABCD'[p // 16], p % 16) if 0 <= p < 64 else 'IO%d' % p


def load(path):
    funcs, cur = collections.OrderedDict(), None
    for l in open(path, encoding='utf-8'):
        if l.startswith('sub_') and l.rstrip().endswith(':'):
            cur = l.strip()[:-1]
            funcs[cur] = []
            continue
        m = LINE.match(l)
        if m and cur:
            funcs[cur].append((int(m.group(1), 16), m.group(2).strip()))
    return funcs


def pin_takers(funcs, seed='sub_01E0019C'):
    """Functions whose r0 reaches a pin taker unchanged (r0 not written before the call)."""
    takers = {seed}
    changed = True
    while changed:
        changed = False
        for name, body in funcs.items():
            if name in takers:
                continue
            for _, ins in body[:12]:
                m = CALL.search(ins)
                if m:
                    if m.group(1) in takers:
                        takers.add(name)
                        changed = True
                    break
                if re.match(r'r0 = |r0 \S=', ins) or ins.startswith('{') and 'r0' in ins.split('=')[0]:
                    break
    return takers


def scan(funcs, takers):
    pins = collections.defaultdict(set)
    sfrs = collections.defaultdict(set)
    for name, body in funcs.items():
        r0 = None
        for _, ins in body:
            m = re.match(r'r0 = (\d+)\s*$', ins.split(';')[0].strip())
            if m:
                r0 = int(m.group(1))
            elif re.match(r'r0 ', ins):
                r0 = None
            c = CALL.search(ins)
            if c:
                if c.group(1) in takers and r0 is not None and r0 < 80:
                    pins[name].add((r0, c.group(1)))
                r0 = None
            for v in re.findall(r'= (\d+)', ins.split(';')[0]):
                v = int(v)
                if 0x1E0000 <= v < 0x200000 or 0x100000 <= v < 0x110000:
                    sfrs[name].add(v)
    return pins, sfrs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dis')
    ap.add_argument('--pins', action='store_true')
    ap.add_argument('--sfr', help='LO-HI hex range')
    ap.add_argument('--func')
    a = ap.parse_args()
    funcs = load(a.dis)
    takers = pin_takers(funcs)
    pins, sfrs = scan(funcs, takers)
    if a.pins:
        by_pin = collections.defaultdict(set)
        for f, s in pins.items():
            for p, via in s:
                by_pin[p].add(f)
        for p in sorted(by_pin):
            print('%-5s %s' % (pname(p), ' '.join(sorted(by_pin[p]))))
    if a.sfr:
        lo, hi = (int(x, 16) for x in a.sfr.split('-'))
        by = collections.defaultdict(set)
        for f, s in sfrs.items():
            for v in s:
                if lo <= v < hi:
                    by[v].add(f)
        for v in sorted(by):
            print('%06X %s' % (v, ' '.join(sorted(by[v]))))
    if a.func:
        print('pins:', sorted((pname(p), v) for p, v in pins.get(a.func, ())))
        print('sfrs:', ' '.join('%06X' % v for v in sorted(sfrs.get(a.func, ()))))
    if not (a.pins or a.sfr or a.func):
        print('pin takers:', ' '.join(sorted(takers)))


if __name__ == '__main__':
    main()
