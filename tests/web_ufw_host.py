#!/usr/bin/env python3
"""The web flasher's .ufw code (web/lib/ufw.js, run in Node) against our Python tools, on the real files.

  python tests/web_ufw_host.py [--app build/fw/firmware.bin]

Needs node, firmware/stock-nrf-v0.27.ufw, firmware/stock-nrfb-v0.04.ufw and MM9PDY's to-stock patch in firmware/,
and a built app. Checks: the JS result is byte for byte what tools/mkufw.py makes, tools/ufwcheck.py passes it, the
JS patch result equals tools/ufwpatch.py's, and the JS refuses NRFB stock and an app without an update mode.
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import json  # noqa: E402
from mkufw import build  # noqa: E402
from ufwpatch import apply_patch  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default=ROOT, help='checkout with firmware/ and build/ (default: this one)')
    ap.add_argument('--app')
    a = ap.parse_args()
    fw = os.path.join(a.root, 'firmware')
    stock = os.path.join(fw, 'stock-nrf-v0.27.ufw')
    nrfb = os.path.join(fw, 'stock-nrfb-v0.04.ufw')
    patch = os.path.join(fw, 'uv5rmini-fw-to-stock-nrf-v0.27-hw1.7-1.1.0.patch.json')
    app = a.app or os.path.join(a.root, 'build', 'fw', 'firmware.bin')
    out = os.path.join(a.root, 'build', 'web-test.ufw')
    r = subprocess.run(['node', os.path.join(ROOT, 'tests', 'web_ufw.mjs'), stock, nrfb, patch, app, out])
    ok = r.returncode == 0
    sb = open(stock, 'rb').read()
    base = apply_patch(json.load(open(patch, encoding='utf-8')), sb)
    same_base = open(out + '.base', 'rb').read() == base
    print('%s JS patch result equals tools/ufwpatch.py' % ('ok  ' if same_base else 'BAD '))
    py = build(base, open(app, 'rb').read())
    same = open(out, 'rb').read() == py
    print('%s JS .ufw equals tools/mkufw.py byte for byte' % ('ok  ' if same else 'BAD '))
    c = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'ufwcheck.py'), out, '--app', app],
                       capture_output=True, text=True)
    print('%s tools/ufwcheck.py: %s' % ('ok  ' if c.returncode == 0 else 'BAD ', c.stdout.strip().splitlines()[-1]))
    os.remove(out + '.base')
    ok = ok and same_base and same and c.returncode == 0
    print('ALL OK' if ok else 'FAILED')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
