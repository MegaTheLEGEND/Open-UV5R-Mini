#!/usr/bin/env python3
"""Build the stock v0.27 firmware from source with the open toolchain, prove it identical, and make it flashable.

usage:
  python tools/build_matching.py --toolchain E:/toolchains/pi32v2-llvm/bin --isa E:/pi32v2-isa
      [--stock firmware/stock-nrf-v0.27.ufw] [--to-stock-patch firmware/uv5rmini-fw-to-stock-...json]
      [--source build/stock/app.S]  (an edited copy of the generated source; default: regenerate it)

Steps, each one checked:
  1. unpack the stock .ufw (tools/jl_ufw_unpack.py)
  2. generate build/stock/app.S from the stock app.bin (tools/mkasm.py), unless --source is given
  3. assemble and link it with clang + ld.lld (matching/stock.ld)       -> build/stock/app.rebuilt.bin
  4. pack it into the stock .ufw (tools/mkufw.py)                        -> build/stock-rebuilt-v0.27.ufw
  5. add the pinned L2 loader with MM9PDY's published to-stock patch     -> build/stock-rebuilt-v0.27-flashable.ufw
     (for an unmodified source this must equal the patch's recorded result)
  6. run the flasher's image checks (tools/flashcheck.py)
  7. write a patch for Codeplug Studio's flasher                         -> build/open-uv5r-mini-<name>.patch.json

With an unmodified source, steps 3, 4 and 5 must reproduce the stock app.bin, the stock .ufw and the published
to-stock image byte for byte, and the script stops if they don't.
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TO_STOCK_URL = ('https://raw.githubusercontent.com/pskillen/mm9pdy-firmware/main/releases/baofeng-uv5rmini/'
                'to-stock/uv5rmini-fw-to-stock-nrf-v0.27-hw1.7-1.1.0.patch.json')
STOCK_SHA = '811b4b37253e0685c7efbd3ff0a24aac2bcad223e5453d95b5bfc3f42c089128'


def run(*cmd):
    print('>', ' '.join(str(c) for c in cmd))
    subprocess.run([str(c) for c in cmd], check=True)


def py(script, *args):
    run(sys.executable, '-I', os.path.join(HERE, script), *args)


def sha(path):
    return hashlib.sha256(open(path, 'rb').read()).hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--toolchain', required=True)
    ap.add_argument('--isa', required=True)
    ap.add_argument('--stock', default=os.path.join(ROOT, 'firmware', 'stock-nrf-v0.27.ufw'))
    ap.add_argument('--to-stock-patch',
                    default=os.path.join(ROOT, 'firmware', 'uv5rmini-fw-to-stock-nrf-v0.27-hw1.7-1.1.0.patch.json'))
    ap.add_argument('--source', help='edited assembly source to build instead of regenerating it')
    ap.add_argument('--name', default='nrf-v0.27',
                    help='build name for the flasher patch (1..21 chars); the stock name means "back to stock"')
    a = ap.parse_args()
    tc = a.toolchain
    b = os.path.join(ROOT, 'build')
    st = os.path.join(b, 'stock')
    os.makedirs(st, exist_ok=True)

    if sha(a.stock) != STOCK_SHA:
        raise SystemExit('%s is not stock-nrf-v0.27.ufw (sha256 %s)' % (a.stock, STOCK_SHA))
    if not os.path.exists(a.to_stock_patch):
        raise SystemExit('missing %s\nget it (unmodified, MM9PDY release patch licence) from:\n  %s'
                         % (a.to_stock_patch, TO_STOCK_URL))

    # 1-2
    py('jl_ufw_unpack.py', a.stock, os.path.join(st, 'unpacked'))
    stock_app = os.path.join(st, 'unpacked', 'app.bin')
    src = a.source or os.path.join(st, 'app.S')
    if not a.source:
        py('mkasm.py', stock_app, '--isa', a.isa, '--toolchain', tc, '-o', src)

    # 3
    obj, elf, rebuilt = (os.path.join(st, n) for n in ('app.o', 'app.elf', 'app.rebuilt.bin'))
    run(os.path.join(tc, 'clang'), '--target=pi32v2', '-c', src, '-o', obj)
    run(os.path.join(tc, 'ld.lld'), '-T', os.path.join(ROOT, 'matching', 'stock.ld'), obj, '-o', elf)
    run(os.path.join(tc, 'llvm-objcopy'), '-O', 'binary', elf, rebuilt)
    identical = open(rebuilt, 'rb').read() == open(stock_app, 'rb').read()
    print('app.bin rebuilt from source is %s the stock app.bin' % ('IDENTICAL to' if identical else 'DIFFERENT from'))
    if not identical and not a.source:
        raise SystemExit('the unmodified source must rebuild the stock app.bin exactly')

    # 4
    ufw = os.path.join(b, 'stock-rebuilt-v0.27.ufw')
    py('mkufw.py', '--base', a.stock, '--app', rebuilt, '-o', ufw)
    if identical:
        if sha(ufw) != STOCK_SHA:
            raise SystemExit('packed .ufw differs from stock')
        print('.ufw rebuilt from source is IDENTICAL to stock-nrf-v0.27.ufw (sha256 %s)' % STOCK_SHA)

    # 5: the reference flashable image = stock + pinned loader, from the published to-stock patch
    ref = os.path.join(b, 'stock-flashable-reference.ufw')
    py('ufwpatch.py', 'apply', a.to_stock_patch, a.stock, '-o', ref)
    flashable = os.path.join(b, 'open-uv5r-mini-%s-flashable.ufw' % a.name)
    py('mkufw.py', '--base', ref, '--app', rebuilt, '-o', flashable)
    if identical:
        want = json.load(open(a.to_stock_patch))['result']['.ufw']['sha256']
        if sha(flashable) != want:
            raise SystemExit('flashable image differs from the published to-stock result')
        print('flashable image is IDENTICAL to the published to-stock image (sha256 %s)' % want)

    # 6
    py('flashcheck.py', flashable, '--stock', a.stock, '--reference', ref)

    # 7
    patch = os.path.join(b, 'open-uv5r-mini-%s.patch.json' % a.name)
    py('ufwpatch.py', 'make', a.stock, flashable, '-o', patch, '--base', 'nrf-v0.27', '--name', a.name,
       '--loader-sha256', '957bb486bb1d8cb5c971e366cd5c45a6da3c64fe611203096b56bb799ab02372')
    print('\nflash with Codeplug Studio: %s (patch) + %s (stock). See docs/TESTING.md.' % (patch, a.stock))


if __name__ == '__main__':
    main()
