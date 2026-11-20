#!/usr/bin/env python3
"""Summarise a JieLi GPIO-setup function in an app.dis listing as a pin table.

usage: python tools/re/gpio_init.py build/app.dis sub_01E485EC
Port block offsets as used by the stock firmware: +0 OUT, +4 IN, +8 DIR (1=input), +12 DIE, +16 PU, +20 PD.
"""
import re, sys
REG = {0: 'OUT', 8: 'DIR', 12: 'DIE', 16: 'PU', 20: 'PD'}

def pname(p):
    return 'P%s%d' % ('ABCD'[p // 16], p % 16) if p < 64 else 'IO%d' % p

def main(dis, func):
    L = open(dis, encoding='utf-8').read().splitlines()
    start = next(i for i, l in enumerate(L) if l.startswith(func + ':'))
    pin, res = None, {}
    for l in L[start + 1:]:
        if l.startswith('sub_'):
            break
        code = l.split('   ;')[0]
        m = re.search(r' r0 = (\d+)\s*$', code)
        if m:
            pin = int(m.group(1)); res.setdefault(pin, {}); continue
        m = re.search(r'\[r\d+\+(\d+)\] (\|=|&=) 0x([0-9A-F]+)', code)
        if m and pin is not None:
            res[pin][REG.get(int(m.group(1)), m.group(1))] = 1 if m.group(2) == '|=' else 0
    print('| GPIO | Pin | Dir | Pull-up | Pull-down | Initial out |\n| --- | --- | --- | --- | --- | --- |')
    for p in sorted(res):
        r = res[p]
        if not r or p >= 64:
            continue
        print(f"| {p} | {pname(p)} | {'in' if r.get('DIR') == 1 else 'out'} | {r.get('PU', '-')} | {r.get('PD', '-')} | {r.get('OUT', '-')} |")

if __name__ == '__main__':
    main(*sys.argv[1:3])
