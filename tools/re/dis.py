#!/usr/bin/env python3
"""Small helpers over build/app.dis: show a function, list callers.

usage: python tools/re/dis.py show sub_XXXXXXXX [maxlines]
       python tools/re/dis.py callers sub_XXXXXXXX
       python tools/re/dis.py around 0x01E4F20C [before] [after]
"""
import re, sys
DIS = 'build/app.dis'

def lines():
    return open(DIS, encoding='utf-8').read().splitlines()

def show(f, n=400):
    L = lines()
    i = next(k for k, l in enumerate(L) if l.startswith(f + ':'))
    print(L[i])
    for l in L[i + 1:i + 1 + int(n)]:
        if l.startswith('sub_'):
            break
        print(l[:110])

def callers(f):
    cur = None
    for l in lines():
        if l.startswith('sub_'):
            cur = l[:-1]
        elif re.search(r'(call|goto) -?\d+\s+; ' + f + r'\b', l) or ('&' + f) in l:
            print(cur, l[:100])

def around(addr, before=30, after=30):
    L = lines(); a = '%08x:' % int(addr, 16)
    i = next(k for k, l in enumerate(L) if l.startswith(a))
    print('\n'.join(x[:110] for x in L[i - int(before):i + int(after)]))

if __name__ == '__main__':
    {'show': show, 'callers': callers, 'around': around}[sys.argv[1]](*sys.argv[2:])
