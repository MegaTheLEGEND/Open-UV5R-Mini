#!/usr/bin/env python3
"""Make the web builder and flasher site (docs/WEB-FLASHER.md) in build/site/, ready to serve or publish.

  python web/tools/mksite.py [--toolchain-wasm DIR] [--sdk build/sdk] [--builtins PATH] [-o build/site]

What goes in (all of it may be published; nothing from Baofeng):
  web/                    the page and its scripts (ours)
  src/                    the firmware sources at this commit (ours), with features.json from features.mk
  sdk/                    JieLi's cpu.a, media.a, btctrler.a and their LICENSE (Apache-2.0; docs/THIRD-PARTY.md)
  toolchain/              llvm.wasm (pi32v2-llvm's clang + ld.lld + llvm-objcopy, web/toolchain/build-wasm.sh),
                          the compiler's headers and compiler-rt's builtins, with LLVM's LICENSE.TXT
The stock .ufw and MM9PDY's to-stock patch are NOT included: the user loads their own in the page.
"""
import argparse
import glob
import gzip
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def parse_features(path):
    """features.mk -> [{name, default, help, values?}] in file order; help = the comment block above the switch."""
    out, comment = [], []
    for line in open(path, encoding='utf-8'):
        s = line.rstrip('\n')
        m = re.match(r'^([A-Z_]+)\s*\?=\s*(\d+)', s)
        if m:
            if comment and comment[0].startswith('Apps (MENU'):     # the Apps section's intro, not this switch's
                ends = [i for i, c in enumerate(comment) if 'section itself goes' in c]
                comment = comment[ends[0] + 1:] if ends else comment
            help_ = ' '.join(comment)
            item = {'name': m.group(1), 'default': int(m.group(2)), 'help': help_}
            if item['name'] == 'REGION':
                item['values'] = {'0': 'chosen in the menu', '1': 'US', '2': 'EU/UK', '3': 'AU'}
            out.append(item)
            comment = []
        elif s.startswith('#'):
            t = s.lstrip('#').strip()
            if t:
                comment.append(t)
            else:
                comment = []
        elif not s.strip():
            comment = []
    return out


def fw_build():
    try:
        return subprocess.run(['git', 'describe', '--always', '--dirty', '--abbrev=7'], cwd=ROOT,
                              capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return 'dev'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-o', '--out', default=os.path.join(ROOT, 'build', 'site'))
    ap.add_argument('--toolchain-wasm', default=os.path.expanduser('~/wasm/out'),
                    help='folder with llvm.wasm and clang-include/ (web/toolchain/build-wasm.sh)')
    ap.add_argument('--sdk', default=os.path.join(ROOT, 'build', 'sdk'))
    ap.add_argument('--builtins', help='pi32v2 libclang_rt.builtins.a (default: from --llvm-release)')
    ap.add_argument('--llvm-release', default='E:/toolchains/pi32v2-llvm',
                    help='an unpacked pi32v2-llvm release (for the builtins and LICENSE.TXT)')
    a = ap.parse_args()
    out = a.out
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)

    # the page
    for p in glob.glob(os.path.join(ROOT, 'web', '**', '*'), recursive=True):
        rel = os.path.relpath(p, os.path.join(ROOT, 'web'))
        if os.path.isfile(p) and not rel.startswith(('tools', 'toolchain')):
            os.makedirs(os.path.dirname(os.path.join(out, rel)), exist_ok=True)
            shutil.copy(p, os.path.join(out, rel))

    # the sources: one JSON {path: text}, so the page fetches one file
    files = {}
    for p in sorted(glob.glob(os.path.join(ROOT, 'src', '**', '*'), recursive=True)):
        if os.path.isfile(p) and p.endswith(('.c', '.h', '.S', '.ld', '.inc')):
            files[os.path.relpath(p, ROOT).replace('\\', '/')] = open(p, encoding='utf-8', newline='').read()
    feats = parse_features(os.path.join(ROOT, 'features.mk'))
    meta = {'build': fw_build(), 'features': feats}
    json.dump({'meta': meta, 'files': files}, open(os.path.join(out, 'sources.json'), 'w', encoding='utf-8'))
    json.dump(meta, open(os.path.join(out, 'meta.json'), 'w', encoding='utf-8'), indent=1)

    # JieLi's libraries with their licence
    os.makedirs(os.path.join(out, 'sdk'))
    for n in ('cpu.a', 'media.a', 'btctrler.a', 'LICENSE.jieli', 'VERSION.txt'):
        src = os.path.join(a.sdk, n)
        if not os.path.exists(src):
            sys.exit('missing %s (sh tools/sdk/fm_libs.sh <sdk clone>)' % src)
        shutil.copy(src, os.path.join(out, 'sdk', n))

    # the toolchain
    tc = os.path.join(out, 'toolchain')
    os.makedirs(tc)
    wasm = os.path.join(a.toolchain_wasm, 'llvm.wasm')
    if not os.path.exists(wasm):
        sys.exit('missing %s (sh web/toolchain/build-wasm.sh)' % wasm)
    with open(wasm, 'rb') as fi, gzip.open(os.path.join(tc, 'llvm.wasm.gz'), 'wb', 9) as fo:
        shutil.copyfileobj(fi, fo)
    hdrs = {}
    inc = os.path.join(a.toolchain_wasm, 'clang-include')
    # only the target-independent C headers (the rest are CUDA, OpenCL and other CPUs' intrinsics)
    keep = re.compile(r'^(__stddef_\w+|__stdarg_\w+|std\w+|float|limits|iso646|inttypes|varargs)\.h$')
    for p in sorted(glob.glob(os.path.join(inc, '*.h'))):
        if not keep.match(os.path.basename(p)):
            continue
        hdrs[os.path.relpath(p, inc).replace('\\', '/')] = open(p, encoding='utf-8', newline='').read()
    json.dump(hdrs, open(os.path.join(tc, 'clang-include.json'), 'w', encoding='utf-8'))
    bl = a.builtins or (glob.glob(os.path.join(a.llvm_release, 'lib', 'clang', '*', 'lib', 'pi32v2',
                                               'libclang_rt.builtins.a')) or [None])[0]
    if not bl:
        sys.exit('no pi32v2 libclang_rt.builtins.a found (--builtins)')
    shutil.copy(bl, os.path.join(tc, 'libclang_rt.builtins.a'))
    shutil.copy(os.path.join(a.llvm_release, 'LICENSE.TXT'), os.path.join(tc, 'LICENSE.TXT'))
    open(os.path.join(out, '.nojekyll'), 'w').close()
    print('site in %s (build %s, %d source files, %d compiler headers, llvm.wasm.gz %.1f MB)' % (
        out, meta['build'], len(files), len(hdrs), os.path.getsize(os.path.join(tc, 'llvm.wasm.gz')) / 1e6))


if __name__ == '__main__':
    main()
