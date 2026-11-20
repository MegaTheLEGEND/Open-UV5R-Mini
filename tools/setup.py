#!/usr/bin/env python3
"""Check your stock firmware files and unpack them into build/: python tools/setup.py

The stock firmware is Baofeng's and is not in this repo. Get the file for your radio (see the README: "Which radio")
and put it in firmware/. This script checks each file against its pinned SHA-256, refuses anything else, and unpacks
the ones it finds:

    firmware/stock-nrf-v0.27.ufw   ->  build/stock/unpacked/        (NRF radios, firmware V0.27)
    firmware/stock-nrfb-v0.04.ufw  ->  build/stock-nrfb/unpacked/   (NRFB radios, firmware V0.04)

Then, for NRF: python tools/disasm.py and python tools/lift/lift.py (see docs/LIFTER.md).
"""
import hashlib
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The unofficial copies Codeplug Studio links to (shared by a third party, not by Baofeng). This repo only links to them.
STOCK = [
    {'name': 'NRF, firmware V0.27', 'file': 'stock-nrf-v0.27.ufw', 'size': 504832,
     'sha256': '811b4b37253e0685c7efbd3ff0a24aac2bcad223e5453d95b5bfc3f42c089128', 'out': 'build/stock/unpacked',
     'url': 'https://mega.nz/file/U5NSDIhZ#DqRVur6RPVay1ylKZFQsz2zrXnR3ZrAMBToPHgPnPvQ'},
    {'name': 'NRFB, firmware V0.04', 'file': 'stock-nrfb-v0.04.ufw', 'size': 520192,
     'sha256': '02419efb44081227faeccf1525bca0e6b0340c9d988ab80b2141c2ee48a7f74e', 'out': 'build/stock-nrfb/unpacked',
     'url': 'https://mega.nz/file/NgFkWZTS#l4b9-Z6dSISLWBNc8OWMHYUfmzyFV_WT9f268APxnt0'},
]


def main():
    found = 0
    for s in STOCK:
        path = os.path.join(ROOT, 'firmware', s['file'])
        if not os.path.exists(path):
            print('%-22s not found: firmware/%s\n%24s download: %s' % (s['name'], s['file'], '', s['url']))
            continue
        data = open(path, 'rb').read()
        if hashlib.sha256(data).hexdigest() != s['sha256']:
            print('%-22s firmware/%s is not the expected file (size %d, expected %d): refused' % (
                s['name'], s['file'], len(data), s['size']))
            continue
        r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'jl_ufw_unpack.py'), path,
                            os.path.join(ROOT, s['out'])], capture_output=True, text=True)
        if r.returncode:
            print('%-22s unpack failed:\n%s' % (s['name'], r.stderr))
            continue
        print('%-22s ok, unpacked to %s/' % (s['name'], s['out']))
        found += 1
    if not found:
        print('\nNo stock file yet. Download the one for your radio from the link above into firmware/, then run this '
              'again.')
    return 0 if found else 1


if __name__ == '__main__':
    sys.exit(main())
