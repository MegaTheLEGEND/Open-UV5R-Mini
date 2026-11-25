#!/usr/bin/env python3
"""Read the radio's cable log (CHIRP's UART, 115200 baud, K1 cable) and write it to a file as it comes, with the
time of each line. Used to see what the FM start finds (src/platform/jl_sdk/fmrx.c, lines starting FMLOG).

    python tools/fmlog_read.py --port COM7 [--out build/fmlog.txt] [--minutes 30]
"""
import argparse
import time

import serial


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', required=True)
    ap.add_argument('--baud', type=int, default=115200)
    ap.add_argument('--out', default='build/fmlog.txt')
    ap.add_argument('--minutes', type=float, default=30)
    a = ap.parse_args()
    end = time.time() + 60 * a.minutes
    buf = b''
    with serial.Serial(a.port, a.baud, timeout=0.5) as s, open(a.out, 'a', encoding='utf-8') as f:
        f.write('--- open %s %s\n' % (a.port, time.strftime('%H:%M:%S')))
        f.flush()
        while time.time() < end:
            buf += s.read(4096)
            while b'\n' in buf:
                line, buf = buf.split(b'\n', 1)
                text = line.decode('ascii', 'replace').strip()
                if text:
                    f.write('%s %s\n' % (time.strftime('%H:%M:%S'), text))
                    f.flush()


if __name__ == '__main__':
    main()
