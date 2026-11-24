#!/usr/bin/env python3
r"""Build and run tests/settings_host.c (src/store/settings.c: the stock VFO and settings logs, read only).

    python tests/settings_host.py

With a dump of your radio's external flash in firmware/backups/external-*.bin (git-ignored), it reads that too.
Needs a host gcc (MSYS2 mingw64 on Windows: put C:\msys64\mingw64\bin on PATH).
"""
import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    out = os.path.join(ROOT, 'build', 'host')
    os.makedirs(out, exist_ok=True)
    exe = os.path.join(out, 'settings_host' + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(['gcc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', '-DCHANNELS_HOST_TEST', '-DSETTINGS_ENABLE_SAVE=1', '-I', os.path.join(ROOT, 'src'),
                    os.path.join(ROOT, 'tests', 'settings_host.c'), os.path.join(ROOT, 'src', 'store', 'settings.c'), os.path.join(ROOT, 'src', 'store', 'channels.c'),
                    os.path.join(ROOT, 'src', 'radio', 'tones.c'), '-o', exe], check=True)
    dumps = sorted(glob.glob(os.path.join(ROOT, 'firmware', 'backups', 'external-*.bin')))
    return subprocess.run([exe] + dumps[-1:]).returncode


if __name__ == '__main__':
    sys.exit(main())
