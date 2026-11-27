#!/usr/bin/env python3
r"""Test src/ui/textentry.c on the PC (tests/textentry_host.c): keypad typing, letters and numbers.
Needs a host gcc (MSYS2 mingw64 on Windows: C:\msys64\mingw64\bin first on PATH).

    python tests/textentry_host.py
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    out = os.path.join(ROOT, 'build', 'host')
    os.makedirs(out, exist_ok=True)
    exe = os.path.join(out, 'textentry_host' + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(['gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-I', os.path.join(ROOT, 'src'),
                    os.path.join(ROOT, 'tests', 'textentry_host.c'), os.path.join(ROOT, 'src', 'ui', 'textentry.c'),
                    '-o', exe], check=True)
    r = subprocess.run([exe], capture_output=True, text=True)
    print(r.stdout.rstrip())
    return r.returncode


if __name__ == '__main__':
    sys.exit(main())
