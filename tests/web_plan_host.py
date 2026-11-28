#!/usr/bin/env python3
"""The web builder's build plan (web/lib/plan.js) against `make -n` for many switch sets.

  python tests/web_plan_host.py [--sets 40]

Needs node, MSYS2 make, and build/sdk/*.a (the FM and Bluetooth switches only count when the libraries are there).
For each switch set: the clang, ld.lld and llvm-objcopy commands make would run must equal the page's, apart from
make's dependency flags (-MMD -MP) and the library paths (build/sdk/ -> sdk/, the builtins).
"""
import argparse
import json
import os
import random
import shlex
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'web', 'tools'))
from mksite import parse_features  # noqa: E402

TC = 'TC'


def make_cmds(sw):
    make = shutil.which('make') or 'C:/msys64/usr/bin/make.exe'
    args = [make, '-n', '-B', 'TOOLCHAIN=' + TC, 'FW_BUILD=web', 'all'] + ['%s=%s' % kv for kv in sw.items()]
    out = subprocess.run(args, cwd=ROOT, capture_output=True, text=True).stdout
    comp, link, objcopy = [], None, None
    for line in out.splitlines():
        if not line.startswith(TC + '/'):
            continue
        w = shlex.split(line)
        tool, rest = w[0][len(TC) + 1:], w[1:]
        if tool == 'clang':
            comp.append([x for x in rest if x not in ('-MMD', '-MP')])
        elif tool == 'ld.lld':
            link = [x.replace('build/sdk/', 'sdk/') for x in rest]
        elif tool == 'llvm-objcopy':
            objcopy = rest
    return comp, link, objcopy


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--sets', type=int, default=40)
    ap.add_argument('--seed', type=int, default=1)
    a = ap.parse_args()
    for n in ('cpu.a', 'media.a', 'btctrler.a'):
        if not os.path.exists(os.path.join(ROOT, 'build', 'sdk', n)):
            sys.exit('needs build/sdk/%s' % n)
    feats = parse_features(os.path.join(ROOT, 'features.mk'))
    defaults = {f['name']: f['default'] for f in feats}
    rnd = random.Random(a.seed)
    sets = [{}, {k: 0 for k in defaults if k != 'REGION'}, {k: 1 for k in defaults if k != 'REGION'}]
    while len(sets) < a.sets:
        sets.append({k: (rnd.choice([0, 1, 2, 3]) if k == 'REGION' else rnd.choice([0, 1])) for k in defaults})
    js = subprocess.run(['node', os.path.join(ROOT, 'tests', 'web_plan.mjs')], input=json.dumps(
        {'defaults': defaults, 'sets': sets}), capture_output=True, text=True, cwd=ROOT)
    if js.returncode:
        sys.exit(js.stderr)
    plans = json.loads(js.stdout)
    bad = 0
    for sw, p in zip(sets, plans):
        comp, link, objcopy = make_cmds(sw)
        want_link = [x for x in p['link'] if not x.endswith('libclang_rt.builtins.a')]
        problems = []
        if comp != [c['args'] for c in p['compile']]:
            mk = {tuple(c) for c in comp}
            pg = {tuple(c['args']) for c in p['compile']}
            problems.append('compile: make only %s; page only %s' % (
                [c[-3] for c in mk - pg][:4], [c[-3] for c in pg - mk][:4]))
        if link != want_link:
            problems.append('link: make %s, page %s' % (link, want_link))
        if objcopy != p['objcopy']:
            problems.append('objcopy differs')
        if problems:
            bad += 1
            print('BAD', sw, *problems, sep='\n  ')
    print('%d switch sets, %d differ' % (len(sets), bad))
    print('ALL OK' if not bad else 'FAILED')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
