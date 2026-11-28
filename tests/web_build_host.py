#!/usr/bin/env python3
"""The in-browser build (WebAssembly clang + ld.lld, web/lib/buildcore.js, run in Node) against the native build.

  python tests/web_build_host.py [--site build/site] [--sets 3] [--commit SHA ...] --toolchain DIR/bin [--wsl]

For a few switch sets at each commit (default: the one the site was made from; any of the site's versions/):
firmware.bin from the site's toolchain must equal, byte for byte, what `make` builds (in a git archive of that
commit) with the native pi32v2-llvm of the SAME version as llvm.wasm (web/toolchain/build-wasm.sh's LLVM_TAG). --wsl runs make
in WSL (for the Linux release, e.g. --toolchain build/llvm-release/bin). Needs node, make, the site
(web/tools/mksite.py) and build/sdk.
"""
import argparse
import io
import tarfile
import tempfile
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SETS = [
    {},                                                        # the defaults
    {'ENABLE_APP_FM': 0, 'ENABLE_BT_LE': 0, 'ENABLE_TX': 0},    # no JieLi libraries, no transmit
    {'ENABLE_CHIRP': 0, 'ENABLE_APP_SNAKE': 0, 'ENABLE_REGIONS': 0, 'ENABLE_DEBUGGING': 1},
]


def unpack(sha, dest):
    data = subprocess.run(['git', 'archive', sha], cwd=ROOT, capture_output=True, check=True).stdout
    tarfile.open(fileobj=io.BytesIO(data)).extractall(dest, filter='data')
    os.makedirs(os.path.join(dest, 'build', 'sdk'))
    for n in ('cpu.a', 'media.a', 'btctrler.a'):
        shutil.copy(os.path.join(ROOT, 'build', 'sdk', n), os.path.join(dest, 'build', 'sdk', n))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--site', default=os.path.join(ROOT, 'build', 'site'))
    ap.add_argument('--sets', type=int, default=len(SETS))
    ap.add_argument('--commit', nargs='*')
    ap.add_argument('--toolchain', required=True)
    ap.add_argument('--wsl', action='store_true')
    a = ap.parse_args()
    index = json.load(open(os.path.join(a.site, 'versions', 'index.json'), encoding='utf-8'))
    shas = [next(c['sha'] for c in index['commits'] if c['sha'].startswith(x)) for x in a.commit or [index['head']]]
    make = shutil.which('make') or 'C:/msys64/usr/bin/make.exe'
    ok = True
    tmp = tempfile.mkdtemp(prefix='webbuild-', dir=os.path.join(ROOT, 'build'))
    try:
        for sha in shas:
            d = os.path.join(tmp, sha[:7])
            unpack(sha, d)
            for sw in SETS[:a.sets]:
                ok &= one(a, make, sha, d, sw)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print('ALL OK' if ok else 'FAILED')
    sys.exit(0 if ok else 1)


def one(a, make, sha, d, sw):
    fw = sha[:7] + '-web'
    web = os.path.join(d, 'web-test.bin')
    r = subprocess.run(['node', os.path.join(ROOT, 'tests', 'web_build.mjs'), a.site, sha, json.dumps(sw), fw, web])
    if r.returncode:
        print('BAD  %s %s: the web build failed' % (sha[:7], sw))
        return False
    args = ['-j8', 'FW_BUILD=' + fw] + ['%s=%s' % kv for kv in sw.items()]
    if a.wsl:
        def wslpath(p):
            return subprocess.run(['wsl', 'wslpath', '-a', os.path.abspath(p).replace(os.sep, '/')],
                                  capture_output=True, text=True).stdout.strip()
        cmd = ['wsl', 'make', '-C', wslpath(d)]
        subprocess.run(cmd + ['clean'], capture_output=True)
        n = subprocess.run(cmd + ['TOOLCHAIN=' + wslpath(a.toolchain)] + args, capture_output=True, text=True)
    else:
        subprocess.run([make, 'clean'], cwd=d, capture_output=True)
        n = subprocess.run([make, 'TOOLCHAIN=' + a.toolchain] + args, cwd=d, capture_output=True, text=True)
    if n.returncode:
        print(n.stdout[-2000:], n.stderr[-2000:])
        print('BAD  %s %s: the native build failed' % (sha[:7], sw))
        return False
    a_bin = open(web, 'rb').read()
    b_bin = open(os.path.join(d, 'build', 'fw', 'firmware.bin'), 'rb').read()
    same = a_bin == b_bin
    print('%s %s %s: web %d bytes, native %d bytes, %s' % ('ok  ' if same else 'BAD ', sha[:7], sw or 'defaults',
                                                         len(a_bin), len(b_bin), 'identical' if same else 'DIFFERENT'))
    return same


if __name__ == '__main__':
    main()
