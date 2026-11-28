#!/usr/bin/env python3
"""The in-browser build (WebAssembly clang + ld.lld, web/lib/buildcore.js, run in Node) against the native build.

  python tests/web_build_host.py [--site build/site] [--sets 3] --toolchain DIR/bin [--wsl]

For a few switch sets: firmware.bin from the site's toolchain must equal, byte for byte, what `make` builds with
the native pi32v2-llvm of the SAME version as llvm.wasm (web/toolchain/build-wasm.sh's LLVM_TAG). --wsl runs make
in WSL (for the Linux release, e.g. --toolchain build/llvm-release/bin). Needs node, make, the site
(web/tools/mksite.py) and build/sdk.
"""
import argparse
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--site', default=os.path.join(ROOT, 'build', 'site'))
    ap.add_argument('--sets', type=int, default=len(SETS))
    ap.add_argument('--toolchain', required=True)
    ap.add_argument('--wsl', action='store_true')
    a = ap.parse_args()
    meta = json.load(open(os.path.join(a.site, 'meta.json'), encoding='utf-8'))
    make = shutil.which('make') or 'C:/msys64/usr/bin/make.exe'
    ok = True
    for sw in SETS[:a.sets]:
        web = os.path.join(ROOT, 'build', 'web-test.bin')
        r = subprocess.run(['node', os.path.join(ROOT, 'tests', 'web_build.mjs'), a.site, json.dumps(sw), web])
        if r.returncode:
            print('BAD  %s: the web build failed' % sw)
            ok = False
            continue
        args = ['-j8', 'FW_BUILD=%s-web' % meta['build']] + ['%s=%s' % kv for kv in sw.items()]
        if a.wsl:
            def wslpath(p):
                return subprocess.run(['wsl', 'wslpath', '-a', os.path.abspath(p).replace(os.sep, '/')],
                                      capture_output=True, text=True).stdout.strip()
            wroot, tc = wslpath(ROOT), wslpath(a.toolchain)
            cmd = ['wsl', 'make', '-C', wroot]
            subprocess.run(cmd + ['clean'], capture_output=True)
            n = subprocess.run(cmd + ['TOOLCHAIN=' + tc] + args, capture_output=True, text=True)
        else:
            subprocess.run([make, 'clean'], cwd=ROOT, capture_output=True)
            n = subprocess.run([make, 'TOOLCHAIN=' + a.toolchain] + args, cwd=ROOT, capture_output=True, text=True)
        if n.returncode:
            print(n.stdout[-2000:], n.stderr[-2000:])
            print('BAD  %s: the native build failed' % sw)
            ok = False
            continue
        a_bin = open(web, 'rb').read()
        b_bin = open(os.path.join(ROOT, 'build', 'fw', 'firmware.bin'), 'rb').read()
        same = a_bin == b_bin
        print('%s %s: web %d bytes, native %d bytes, %s' % ('ok  ' if same else 'BAD ', sw or 'defaults', len(a_bin),
                                                          len(b_bin), 'identical' if same else 'DIFFERENT'))
        ok &= same
    print('ALL OK' if ok else 'FAILED')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
