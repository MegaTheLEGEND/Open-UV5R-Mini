#!/usr/bin/env python3
"""List every call to a function with the constant arguments set up just before it (r0..r3).

usage: python tools/re/callers.py build/app.dis sub_01E400D6 [more ...]
"""
import re
import sys


def main():
    path, targets = sys.argv[1], set(sys.argv[2:])
    L = open(path, encoding='utf-8').read().splitlines()
    func = None
    for i, l in enumerate(L):
        if l.startswith('sub_') and l.rstrip().endswith(':'):
            func = l.strip()[:-1]
        m = re.search(r'(call|goto) .*?(sub_[0-9A-F]{8})', l)
        if not m or m.group(2) not in targets:
            continue
        args = {}
        for back in L[max(0, i - 8):i][::-1]:
            code = back.split('   ;')[0]
            if re.search(r'call |rts|\{pc', code):
                break
            a = re.search(r' (r[0-3]) = (-?\d+|r\d+)\s*$', code)
            if a and a.group(1) not in args:
                args[a.group(1)] = a.group(2)
        print('%s %s -> %s  %s' % (l[:8], func, m.group(2),
                                  ' '.join('%s=%s' % (k, args[k]) for k in sorted(args))))


if __name__ == '__main__':
    main()
