#!/usr/bin/env python3
"""Compare the NRF (V0.27) and NRFB (V0.04) stock apps function by function: python tools/variant_diff.py

Needs both lifted: python tools/lift/lift.py, and python tools/lift/lift.py --variant nrfb (docs/LIFTER.md).
Functions are matched by their lifted C with every address blanked out. It prints how many are identical,
how many changed (paired by similarity: a rough guide), and which are new or gone, and writes build/nrfb/diff.json.
"""
import collections
import difflib
import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def load(d):
    src = open(d + '/lifted.c', encoding='utf-8').read()
    info = json.load(open(d + '/functions.json'))
    out = {}
    for m in re.finditer(r'^void (f_[0-9A-F]{8})\(void\)\n\{\n(.*?)^\}\n', src, re.S | re.M):
        n = m.group(1)
        if not info[n]['live']:
            continue
        body = m.group(2)
        body = re.sub(r'L_[0-9A-F]{8}', 'L', body)
        body = re.sub(r'LIFT_INSN\(0x[0-9A-F]+u\);', '', body)
        body = re.sub(r'0x0?1E[0-9A-F]{5}u', 'ADDR', body)
        body = re.sub(r'/\*.*?\*/', '', body)
        body = re.sub(r'case 0x[0-9A-F]+u', 'case', body)
        out[n] = (body, info[n])
    return out

A = load(os.path.join(ROOT, 'build', 'lift')); B = load(os.path.join(ROOT, 'build', 'nrfb', 'lift'))
ka = collections.defaultdict(list); kb = collections.defaultdict(list)
for n, (b, i) in A.items(): ka[b].append(n)
for n, (b, i) in B.items(): kb[b].append(n)
same = set(ka) & set(kb)
onlyA = [n for b in ka if b not in kb for n in ka[b]]
onlyB = [n for b in kb if b not in ka for n in kb[b]]
print('NRF code functions', len(A), 'NRFB', len(B))
print('identical apart from addresses:', sum(len(ka[b]) for b in same), 'NRF /', sum(len(kb[b]) for b in same), 'NRFB')
print('changed or only in NRF:', len(onlyA), ' changed or only in NRFB:', len(onlyB))

# pair the leftovers: nearest by order (address) and similarity
def ratio(x, y):
    return difflib.SequenceMatcher(None, x, y, autojunk=False).quick_ratio()
pairs = []; usedB = set()
for a in onlyA:
    best = max(((ratio(A[a][0], B[b][0]), b) for b in onlyB if b not in usedB and abs(len(A[a][0]) - len(B[b][0])) < 0.5 * len(A[a][0]) + 200), default=(0, None))
    if best[0] > 0.6:
        pairs.append((a, best[1], best[0])); usedB.add(best[1])
newB = [b for b in onlyB if b not in usedB]
gone = [a for a in onlyA if a not in {p[0] for p in pairs}]
print('changed (paired):', len(pairs), ' new in NRFB:', len(newB), ' gone from NRF:', len(gone))
rows = []
for a, b, r in pairs:
    la, lb = A[a][0].count('\n'), B[b][0].count('\n')
    sa = set(A[a][1]['strings']); sb = set(B[b][1]['strings'])
    rows.append((abs(lb - la), a, b, la, lb, sorted(sb - sa)[:3], sorted(sa - sb)[:3], A[a][1]['strings'][:3]))
for r in sorted(rows, reverse=True)[:40]:
    print('%s -> %s  lines %d -> %d  +str %s -str %s  uses %s' % r[1:])
print('NEW in NRFB:')
for b in newB[:30]: print(' ', b, B[b][0].count('\n'), 'lines', B[b][1]['strings'][:4])
print('GONE from NRF:')
for a in gone[:30]: print(' ', a, A[a][0].count('\n'), 'lines', A[a][1]['strings'][:4])
json.dump({'pairs': pairs, 'new': newB, 'gone': gone}, open(os.path.join(ROOT, 'build', 'nrfb', 'diff.json'), 'w'))
