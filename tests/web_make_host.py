#!/usr/bin/env python3
"""The web builder's make (web/lib/make.js, in Node) against GNU make, at many commits and switch sets.

  python tests/web_make_host.py [--commits N] [--sets 4]

For each commit that changed the Makefile or features.mk (newest first, up to --commits): unpack it (git archive)
with build/sdk/*.a and a stand-in toolchain folder, run `make -n -B all` with several switch sets, and compare every
command line with what make.js lists, and web/lib/features.js's reading of features.mk with mksite.py's. Needs node, git, make (MSYS2 on Windows) and build/sdk/*.a.
"""
import argparse
import io
import json
import os
import random
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'web', 'tools'))
from mksite import parse_features  # noqa: E402

TC = 'tc/bin'
BUILTINS = 'tc/lib/clang/23/lib/pi32v2/libclang_rt.builtins.a'


def unpack(sha, dest):
    data = subprocess.run(['git', 'archive', sha], cwd=ROOT, capture_output=True, check=True).stdout
    tarfile.open(fileobj=io.BytesIO(data)).extractall(dest, filter='data')
    os.makedirs(os.path.join(dest, 'build', 'sdk'))
    for n in ('cpu.a', 'media.a', 'btctrler.a'):
        shutil.copy(os.path.join(ROOT, 'build', 'sdk', n), os.path.join(dest, 'build', 'sdk', n))
    os.makedirs(os.path.dirname(os.path.join(dest, BUILTINS)))
    open(os.path.join(dest, BUILTINS), 'wb').close()
    os.makedirs(os.path.join(dest, TC))


def make_cmds(d, sw):
    make = shutil.which('make') or 'C:/msys64/usr/bin/make.exe'
    r = subprocess.run([make, '-n', '-B', 'TOOLCHAIN=' + TC, 'FW_BUILD=web', 'all'] + ['%s=%s' % kv for kv in sw.items()],
                       cwd=d, capture_output=True, text=True)
    if r.returncode:
        return None, r.stderr.strip().splitlines()[-1:]
    return [l for l in r.stdout.splitlines() if l.strip()], None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--commits', type=int, default=60)
    ap.add_argument('--sets', type=int, default=4)
    a = ap.parse_args()
    shas = subprocess.run(['git', 'log', '--format=%h', '-n', str(a.commits), '--', 'Makefile', 'features.mk'],
                          cwd=ROOT, capture_output=True, text=True).stdout.split()
    rnd = random.Random(1)
    bad = runs = 0
    os.makedirs(os.path.join(ROOT, 'build'), exist_ok=True)
    tmp = tempfile.mkdtemp(prefix='webmake-', dir=os.path.join(ROOT, 'build'))   # on this drive, not the system temp
    try:
        for sha in shas:
            d = os.path.join(tmp, sha)
            unpack(sha, d)
            fm = os.path.join(d, 'features.mk')
            feats = parse_features(fm) if os.path.exists(fm) else []
            names = [f['name'] for f in feats]
            sets = [{}, {n: 0 for n in names if n != 'REGION'}]
            while len(sets) < a.sets:
                sets.append({n: (rnd.choice([0, 1, 2, 3]) if n == 'REGION' else rnd.choice([0, 1])) for n in names})
            if not names:
                sets = [{}]
            files = {}
            for base, _, fs in os.walk(d):
                for f in fs:
                    p = os.path.relpath(os.path.join(base, f), d).replace(os.sep, '/')
                    text = p == 'Makefile' or p.endswith('.mk')
                    files[p] = open(os.path.join(base, f), encoding='utf-8', errors='replace').read() if text else 1
            js = subprocess.run(['node', os.path.join(ROOT, 'tests', 'web_make.mjs')], input=json.dumps(
                {'files': files, 'sets': sets, 'tc': TC}), capture_output=True, text=True)
            if js.returncode:
                print('BAD  %s: node failed: %s' % (sha, js.stderr.strip()[-300:]))
                bad += 1
                continue
            res = json.loads(js.stdout)
            if res['features'] != feats:
                print('BAD  %s: features.js reads features.mk differently' % sha)
                bad += 1
            for sw, g in zip(sets, res['runs']):
                runs += 1
                want, err = make_cmds(d, sw)
                if want is None:
                    ok = 'error' in g
                    if not ok:
                        print('BAD  %s set %d: make failed (%s) but make.js did not' % (sha, sets.index(sw), err))
                elif 'error' in g:
                    ok = False
                    print('BAD  %s set %d: make.js: %s' % (sha, sets.index(sw), g['error']))
                else:
                    ok = want == g['commands']
                    if not ok:
                        diff = next(((i, w, x) for i, (w, x) in enumerate(zip(want, g['commands'])) if w != x), None)
                        print('BAD  %s set %d: %d vs %d lines; first difference %s' % (
                            sha, sets.index(sw), len(want), len(g['commands']), diff or 'length'))
                bad += not ok
            print('     %s: %d switch sets, %d switches' % (sha, len(sets), len(names)))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print('%d commits, %d runs, %d differ' % (len(shas), runs, bad))
    print('ALL OK' if not bad else 'FAILED')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
