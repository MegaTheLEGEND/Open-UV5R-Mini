#!/usr/bin/env python3
r"""Test src/bt/le_adv.c on the PC (tests/btle_host.c): BLE advertising data parsed, the list of devices heard,
and the GATT serial server (src/bt/gatt.c).
Needs a host gcc (MSYS2 mingw64 on Windows: C:\msys64\mingw64\bin first on PATH).

    python tests/btle_host.py
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    out = os.path.join(ROOT, 'build', 'host')
    os.makedirs(out, exist_ok=True)
    exe = os.path.join(out, 'btle_host' + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(['gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-I', os.path.join(ROOT, 'src'),
                    os.path.join(ROOT, 'tests', 'btle_host.c'), os.path.join(ROOT, 'src', 'bt', 'le_adv.c'),
                    os.path.join(ROOT, 'src', 'bt', 'gatt.c'),
                    '-o', exe], check=True)
    r = subprocess.run([exe], capture_output=True, text=True)
    print(r.stdout.rstrip())
    return r.returncode


if __name__ == '__main__':
    sys.exit(main())
