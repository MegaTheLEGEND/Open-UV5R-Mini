#!/usr/bin/env python3
"""Show each place a function sets up a pin and what it does to the port block right after.

usage: python tools/re/pin_uses.py build/app.dis PIN [PIN ...]       PIN as a number or a name like PB8

For each call of GPIO_PortFromPin (sub_01E0019C) with that pin in r0, prints the function, the address and the
next few port-register operations (+0 OUT, +4 IN, +8 DIR, +12 DIE, +16 PU, +20 PD; JieLi port block layout).
"""
import re
import sys

REG = {0: 'OUT', 4: 'IN', 8: 'DIR', 12: 'DIE', 16: 'PU', 20: 'PD'}


def pnum(s):
    if s[0] == 'P':
        return 'ABCD'.index(s[1]) * 16 + int(s[2:])
    return int(s)


def main():
    path, pins = sys.argv[1], [pnum(p) for p in sys.argv[2:]]
    L = open(path, encoding='utf-8').read().splitlines()
    func = None
    for i, l in enumerate(L):
        if l.startswith('sub_') and l.rstrip().endswith(':'):
            func = l.strip()[:-1]
        if 'call' in l and 'sub_01E0019C' in l:
            pin = None
            for back in L[max(0, i - 6):i][::-1]:
                m = re.search(r' r0 = (\d+)\s*$', back.split('   ;')[0])
                if m:
                    pin = int(m.group(1))
                    break
            if pin not in pins:
                continue
            ops = []
            for nxt in L[i + 1:i + 14]:
                code = nxt.split('   ;')[0]
                m = re.search(r'\[r0\+(\d+)\] (\|=|&=|=) (\S+)', code)
                if m:
                    ops.append('%s %s %s' % (REG.get(int(m.group(1)), m.group(1)), m.group(2), m.group(3)))
                m = re.search(r'= \[r0\+(\d+)\]', code)
                if m:
                    ops.append('read %s' % REG.get(int(m.group(1)), m.group(1)))
                if 'call' in code or 'rts' in code:
                    break
            print('P%s%-3d %s %s  %s' % ('ABCD'[pin // 16], pin % 16, func, L[i][:8], '; '.join(ops)))


if __name__ == '__main__':
    main()
