#!/usr/bin/env python3
r"""Build and run tests/radio_host.c (src/radio/radio.c with stand-in drivers) twice: transmit compiled off, and on.

    python tests/radio_host.py

Needs a host gcc (MSYS2 mingw64 on Windows: put C:\msys64\mingw64\bin on PATH).
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    out = os.path.join(ROOT, 'build', 'host')
    os.makedirs(out, exist_ok=True)
    rc = 0
    for tx in (0, 1):
        exe = os.path.join(out, 'radio_host_tx%d%s' % (tx, '.exe' if os.name == 'nt' else ''))
        subprocess.run(['gcc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', '-DRADIO_ENABLE_TX=%d' % tx,
                        '-I', os.path.join(ROOT, 'tests', 'stubs'), '-I', os.path.join(ROOT, 'src'),
                        os.path.join(ROOT, 'tests', 'radio_host.c'), os.path.join(ROOT, 'src', 'radio', 'radio.c'), os.path.join(ROOT, 'src', 'radio', 'txlock.c'),
                        os.path.join(ROOT, 'src', 'store', 'calib.c'), os.path.join(ROOT, 'src', 'store', 'channels.c'), os.path.join(ROOT, 'src', 'radio', 'tones.c'), os.path.join(ROOT, 'src', 'app', 'battery.c'), os.path.join(ROOT, 'src', 'radio', 'scan.c'), os.path.join(ROOT, 'src', 'radio', 'dualwatch.c'), os.path.join(ROOT, 'src', 'radio', 'vox.c'), os.path.join(ROOT, 'src', 'radio', 'dtmfseq.c'), os.path.join(ROOT, 'src', 'radio', 'ax25.c'), '-DCHANNELS_HOST_TEST',
                        '-o', exe], check=True)
        rc |= subprocess.run([exe]).returncode
    return rc


if __name__ == '__main__':
    sys.exit(main())
