#!/usr/bin/env python3
"""Save the radio's last sub-GHz capture (MENU -> APPS -> SUB-GHZ, a debug build) as a Flipper Zero .sub file, through
tools/radiomon.py (which must be running with the cable plugged in).

    python tools/subget.py doorbell.sub          # the decoded key if one was found, else RAW
    python tools/subget.py doorbell_raw.sub --raw
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import radiomon  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--raw', action='store_true')
    a = ap.parse_args()
    reply = radiomon.send('app sub raw' if a.raw else 'app sub', 15.0).splitlines()
    if not reply or reply[-1] != '@ok':
        print('\n'.join(reply))
        return 1
    lines = [l[1:] for l in reply[:-1]]
    with open(a.out, 'w', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    print('wrote %s (%d lines)' % (a.out, len(lines)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
