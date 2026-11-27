#!/usr/bin/env python3
r"""Test src/radio/ax25.c, aprs.c and afsk.c on the PC: builds tests/aprs_host.c with the host gcc and runs it
(frames, APRS packets described, settings packed, distance, and our beacon as Bell 202 audio through the software
demodulator, with noise). Needs a host gcc (MSYS2 mingw64 on Windows: C:\msys64\mingw64\bin first on PATH).

    python tests/aprs_host.py
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    out = os.path.join(ROOT, 'build', 'host')
    os.makedirs(out, exist_ok=True)
    exe = os.path.join(out, 'aprs_host' + ('.exe' if os.name == 'nt' else ''))
    src = [os.path.join(ROOT, 'tests', 'aprs_host.c')] + \
          [os.path.join(ROOT, 'src', 'radio', f) for f in ('ax25.c', 'aprs.c', 'afsk.c')]
    subprocess.run(['gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-D_USE_MATH_DEFINES',
                    '-I', os.path.join(ROOT, 'src')] + src + ['-o', exe, '-lm'], check=True)
    r = subprocess.run([exe], capture_output=True, text=True)
    print(r.stdout.rstrip())
    return r.returncode


if __name__ == '__main__':
    sys.exit(main())
