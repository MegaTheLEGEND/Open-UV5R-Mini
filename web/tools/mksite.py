#!/usr/bin/env python3
"""Make the web builder and flasher site (docs/WEB-FLASHER.md) in build/site/, ready to serve or publish.

  python web/tools/mksite.py [--toolchain-wasm DIR] [--sdk build/sdk] [--builtins PATH] [-o build/site]

What goes in (all of it may be published; nothing from Baofeng):
  web/                    the page and its scripts (ours)
  versions/               the firmware sources (ours) of each release (tag) and recent commits, read from git:
                          index.json (what there is), <sha>.json (that commit's build files: Makefile, *.mk, src/).
                          Any other commit the page fetches from GitHub itself.
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


# What a build needs from a commit: the Makefile and the .mk files at the top, the sources and the linker script
BUILD_EXT = ('.c', '.h', '.S', '.s', '.ld', '.inc', '.mk')


def is_build_file(path):
    if '/' not in path:
        return path == 'Makefile' or path.endswith('.mk')
    return path.startswith('src/') and path.endswith(BUILD_EXT)


def git(*args, **kw):
    return subprocess.run(['git'] + list(args), cwd=ROOT, capture_output=True, check=True, **kw).stdout


def commit_files(sha):
    """{path: text} of the build files at a commit."""
    paths = []
    for line in git('ls-tree', '-r', sha).decode('utf-8', 'replace').splitlines():
        meta, path = line.split('\t', 1)
        mode, kind, obj = meta.split()
        if kind == 'blob' and mode != '120000' and is_build_file(path):
            paths.append((path, obj))
    data = git('cat-file', '--batch', input=''.join(o + '\n' for _, o in paths).encode())
    files, i = {}, 0
    for path, obj in paths:
        nl = data.index(b'\n', i)
        size = int(data[i:nl].split()[2])
        files[path] = data[nl + 1:nl + 1 + size].decode('utf-8', 'replace')
        i = nl + 1 + size + 1
    return files


def features_of(files):
    """parse_features of a commit's features.mk text ([] without one)."""
    text = files.get('features.mk')
    if text is None:
        return []
    tmp = os.path.join(ROOT, 'build', 'features-%d.mk' % os.getpid())
    os.makedirs(os.path.dirname(tmp), exist_ok=True)
    with open(tmp, 'w', encoding='utf-8', newline='') as f:
        f.write(text)
    try:
        return parse_features(tmp)
    finally:
        os.unlink(tmp)


def repo_name():
    if os.environ.get('GITHUB_REPOSITORY'):
        return os.environ['GITHUB_REPOSITORY']
    try:
        url = git('remote', 'get-url', 'origin', text=True).strip()
    except subprocess.CalledProcessError:
        return ''
    m = re.search(r'github\.com[:/]([^/]+/[^/]+?)(\.git)?$', url)
    return m.group(1) if m else ''


def make_versions(out, recent):
    """versions/index.json and versions/<sha>.json for every tag, the newest `recent` commits that changed the build
    on any branch, and HEAD. -> the index"""
    vd = os.path.join(out, 'versions')
    os.makedirs(vd)

    def log(*a):
        rows = []
        for line in git('log', '--format=%H%x00%cs%x00%ct%x00%s', *a, text=True).splitlines():
            sha, date, t, subj = line.split('\x00', 3)
            rows.append({'sha': sha, 'date': date, 'time': int(t), 'subject': subj})
        return rows
    head = git('rev-parse', 'HEAD', text=True).strip()
    releases = []
    for tag in git('tag', '--sort=-creatordate', text=True).split():
        releases.append({'tag': tag, **log('-n', '1', tag + '^{commit}')[0]})
    branches = {}
    for ref in git('for-each-ref', '--format=%(refname)', 'refs/heads', 'refs/remotes/origin', text=True).split():
        name = re.sub(r'^refs/(heads|remotes/origin)/', '', ref)
        if name != 'HEAD':
            branches.setdefault(name, ref)
    commits = {}
    for row in log('-n', str(recent), '--all', '--', 'Makefile', '*.mk', 'src'):
        commits[row['sha']] = row
    for r in releases:
        commits.setdefault(r['sha'], {k: r[k] for k in ('sha', 'date', 'time', 'subject')})
    for ref in [head] + list(branches.values()):     # HEAD and each branch's newest commit
        row = log('-n', '1', ref)[0]
        commits.setdefault(row['sha'], row)
    for name, ref in sorted(branches.items()):      # the branches each commit is on, and which it is the tip of
        tip = git('rev-parse', ref, text=True).strip()
        on = set(git('rev-list', '-n', '500', ref, text=True).split())
        for sha, c in commits.items():
            if sha in on:
                c.setdefault('branches', []).append(name)
            if sha == tip:
                c.setdefault('tips', []).append(name)
    total = 0
    for sha, c in commits.items():
        files = commit_files(sha)
        feats = features_of(files)
        c['switches'] = len(feats)
        body = json.dumps({'sha': sha, 'files': files, 'features': feats})
        total += len(body)
        with open(os.path.join(vd, sha + '.json'), 'w', encoding='utf-8') as f:
            f.write(body)
    index = {'repo': repo_name(), 'head': head, 'built': fw_build(), 'releases': releases,
             'branches': sorted(branches), 'commits': sorted(commits.values(), key=lambda c: c['time'], reverse=True)}
    with open(os.path.join(vd, 'index.json'), 'w', encoding='utf-8') as f:
        json.dump(index, f, indent=1)
    print('versions: %d commits, %d releases, %.1f MB' % (len(commits), len(releases), total / 1e6))
    return index


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
    ap.add_argument('--recent', type=int, default=40, help='recent commits to include, besides every tag and HEAD')
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

    # the firmware sources of each version (from git: commits, not uncommitted changes)
    index = make_versions(out, a.recent)
    meta = {'build': index['built'], 'repo': index['repo'], 'head': index['head']}
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
    print('site in %s (build %s, %d compiler headers, llvm.wasm.gz %.1f MB)' % (
        out, meta['build'], len(hdrs), os.path.getsize(os.path.join(tc, 'llvm.wasm.gz')) / 1e6))


if __name__ == '__main__':
    main()
