#!/usr/bin/env python3
"""Talk to the radio's debug monitor on the cable (src/platform/jl_sdk/fmrx.c, FM_MONITOR) and log what it says.

Everything the radio sends goes to --out with the time, as tools/fmlog_read.py does. Commands are taken from
--cmds: append a line to that file and it is sent to the radio (one command a line, see mon_cmd in fmrx.c).

    python tools/fmmon.py --port COM7 [--out build/fmlog.txt] [--cmds build/mon_in.txt] [--minutes 120]
"""
import argparse
import os
import time

import serial


def stamp():
    t = time.time()
    return time.strftime('%H:%M:%S', time.localtime(t)) + '.%03d' % (int(t * 1000) % 1000)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', required=True)
    ap.add_argument('--baud', type=int, default=115200)
    ap.add_argument('--out', default='build/fmlog.txt')
    ap.add_argument('--cmds', default='build/mon_in.txt')
    ap.add_argument('--minutes', type=float, default=120)
    a = ap.parse_args()
    end = time.time() + 60 * a.minutes
    open(a.cmds, 'a').close()
    sent = os.path.getsize(a.cmds)                  # only commands added from now on
    while time.time() < end:                        # the port can drop (cable pulled, radio reset): open it again
        try:
            run(a, end, sent)
            return
        except (serial.SerialException, OSError) as e:
            with open(a.out, 'a', encoding='utf-8') as f:
                f.write('%s --- port error: %s\n' % (stamp(), e))
            sent = os.path.getsize(a.cmds)
            time.sleep(2)


def run(a, end, sent):
    buf = b''
    with serial.Serial(a.port, a.baud, timeout=0.1) as s, open(a.out, 'a', encoding='utf-8') as f:
        f.write('--- open %s %s (monitor)\n' % (a.port, time.strftime('%H:%M:%S')))
        f.flush()
        while time.time() < end:
            buf += s.read(4096)
            while b'\n' in buf:
                line, buf = buf.split(b'\n', 1)
                text = line.decode('ascii', 'replace').strip()
                if text:
                    f.write('%s %s\n' % (stamp(), text))
                    f.flush()
            size = os.path.getsize(a.cmds)
            if size > sent:
                with open(a.cmds, 'rb') as c:
                    c.seek(sent)
                    new = c.read()
                cut = new.rfind(b'\n') + 1
                for cmd in new[:cut].splitlines():
                    cmd = cmd.strip()
                    if cmd:
                        f.write('%s >> %s\n' % (stamp(), cmd.decode('ascii', 'replace')))
                        f.flush()
                        s.write(cmd + b'\r\n')
                        time.sleep(0.05)
                sent += cut


if __name__ == '__main__':
    main()
