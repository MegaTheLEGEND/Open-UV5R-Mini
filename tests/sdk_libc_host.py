#!/usr/bin/env python3
r"""Build and run tests/sdk_libc_host.c: checks tools/sdk/libc (the small C library for building JieLi's BR23 SDK)
against the host's C library.

    python tests/sdk_libc_host.py

The library's sources are built with gcc against their own headers, and their symbols renamed with a t_ prefix
(objcopy), so both libraries can be linked into one test. Needs Linux gcc: mingw's headers bring their own C runtime
types into a freestanding build, so on Windows this re-runs itself in WSL (`wsl python3 ...`). The checks compare
with the host's C library, whose `long` is 64 bits; on the radio it is 32, which the code handles through LONG_MAX.
"""
import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIBC = os.path.join(ROOT, 'tools', 'sdk', 'libc')


def wsl_path(p):
    p = os.path.abspath(p)
    return '/mnt/' + p[0].lower() + p[2:].replace('\\', '/')


def main():
    if os.name == 'nt':
        return subprocess.run(['wsl', 'python3', wsl_path(__file__)]).returncode
    out =os.path.join(ROOT, 'build', 'host', 'sdk_libc')
    os.makedirs(out, exist_ok=True)
    objs = []
    for src in sorted(glob.glob(os.path.join(LIBC, 'src', '*.c'))):
        obj = os.path.join(out, os.path.basename(src)[:-2] + '.o')
        subprocess.run(['gcc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-builtin',
                        '-I', os.path.join(LIBC, 'include'), '-c', src, '-o', obj], check=True)
        subprocess.run(['objcopy', '--prefix-symbols=t_', obj], check=True)
        objs.append(obj)
    exe = os.path.join(out, 'sdk_libc_host' + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(['gcc', '-std=c11', '-O1', '-Wall', os.path.join(ROOT, 'tests', 'sdk_libc_host.c')] + objs +
                   ['-lm', '-o', exe], check=True)
    return subprocess.run([exe]).returncode


if __name__ == '__main__':
    sys.exit(main())
