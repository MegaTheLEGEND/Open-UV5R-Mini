#!/usr/bin/env python3
"""The web flasher's update session (web/lib/flasher.js, in Node) against tools/uv5r_flasher.py's emulated radio.

  python tests/web_flasher_host.py [--root CHECKOUT] [--app build/fw/firmware.bin]

The emulated radio runs here; Node talks to it over a local TCP bridge that keeps the cable's rule (bytes sent at a
rate the other end isn't set to are lost). Two runs: a whole update, and one with the power cut half-way and a
second session that resumes it. After each, the emulated flash must equal the image and pass the boot check.
"""
import argparse
import json
import os
import socket
import subprocess
import sys
import threading

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from mkufw import build  # noqa: E402
from ufwpatch import apply_patch  # noqa: E402
from uv5r_flasher import AREA_START, FLASH_SOC, EmuRadio, _Wire, ufw_files  # noqa: E402


class BridgeLink:
    """The host end of the emulated cable, as EmuRadio expects, fed by one TCP connection."""

    def __init__(self, radio, conn):
        self.to_radio, self.to_host = _Wire(), _Wire()
        self.radio, self.conn = radio, conn
        radio.plug(self)
        threading.Thread(target=self._out, daemon=True).start()
        f = conn.makefile('r')
        for line in f:
            if line.startswith('W '):
                _, baud, hx = line.split()
                self.to_radio.send(bytes.fromhex(hx), int(baud))
        radio.unplug()

    def _out(self):
        off_sent = False
        while True:
            try:
                b, baud = self.to_host.q.get(timeout=0.05)
                self.conn.sendall(('R %d %s\n' % (baud, b.hex())).encode())
            except Exception:  # noqa: BLE001 (queue.Empty or a closed socket)
                if not off_sent and not self.radio.thread.is_alive() and self.to_host.q.empty():
                    off_sent = True
                    try:
                        self.conn.sendall(b'X\n')
                    except OSError:
                        return
                if getattr(self.conn, '_closed', False):
                    return


def run(radio, img_path, sessions, cut):
    srv = socket.socket()
    srv.bind(('127.0.0.1', 0))
    srv.listen(1)
    port = srv.getsockname()[1]

    def serve():
        for _ in range(sessions):
            c, _ = srv.accept()
            BridgeLink(radio, c)
            c.close()
    t = threading.Thread(target=serve, daemon=True)
    t.start()
    r = subprocess.run(['node', os.path.join(ROOT, 'tests', 'web_flasher.mjs'), str(port), img_path, str(sessions)],
                       capture_output=True, text=True, timeout=600)
    print(r.stdout.rstrip())
    if r.returncode:
        print(r.stderr.rstrip())
    return r.returncode == 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default=ROOT)
    ap.add_argument('--app')
    a = ap.parse_args()
    fw = os.path.join(a.root, 'firmware')
    stock = open(os.path.join(fw, 'stock-nrf-v0.27.ufw'), 'rb').read()
    patch = json.load(open(os.path.join(fw, 'uv5rmini-fw-to-stock-nrf-v0.27-hw1.7-1.1.0.patch.json'), encoding='utf-8'))
    app = open(a.app or os.path.join(a.root, 'build', 'fw', 'firmware.bin'), 'rb').read()
    img = build(apply_patch(patch, stock), app)
    img_path = os.path.join(a.root, 'build', 'web-flash-test.ufw')
    open(img_path, 'wb').write(img)
    _, f = ufw_files(img)
    o, n, _ = f['flash.bin']
    ok_all = True
    for name, cut in (('whole update', None), ('power cut at 50%, then resumed', 0.5)):
        radio = EmuRadio(stock)
        radio.log = lambda s: None
        radio.cut_at = cut
        ok = run(radio, img_path, 2 if cut else 1, cut)
        import time
        time.sleep(0.3)
        bok, got_app, bad = radio.boot_check()
        same = radio.flash[AREA_START:FLASH_SOC + n] == img[o + AREA_START - FLASH_SOC:o + n]
        good = ok and bok and same and got_app == app + b'\xFF' * (len(got_app) - len(app))
        print('%s %s: session %s, flash equals image %s, boot check %s' % (
            'ok  ' if good else 'BAD ', name, ok, same, 'ok' if bok else ', '.join(bad)))
        ok_all &= good
    os.remove(img_path)
    print('ALL OK' if ok_all else 'FAILED')
    sys.exit(0 if ok_all else 1)


if __name__ == '__main__':
    main()
