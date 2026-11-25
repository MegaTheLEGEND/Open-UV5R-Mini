#!/usr/bin/env python3
r"""Run CHIRP's own, unchanged UV-5R Mini driver against our programming mode (src/app/program.c) on the PC.

    python tests/chirp_host.py [--chirp PATH_TO_CHIRP_CHECKOUT] [--link cable|bt|all]

Needs a checkout of CHIRP (git clone https://github.com/kk7ds/chirp; default: ../chirp next to this repo, or
E:/chirp-ref/chirp) and a host gcc. The external flash starts as your radio's dump (firmware/backups/external-*.bin,
git-ignored) if there is one, else empty with seeded settings. Checks: download matches the flash; a channel edited
in CHIRP and uploaded lands in flash; a second download returns exactly what was uploaded; an unchanged upload erases
nothing; nothing at or past 0xB000 (calibration 0xF000, fonts) changes.

Links (default all): "cable" is the open build (no PROGRAM_BT) over the fake UART; "bt" is the Bluetooth edition's
build (PROGRAM_BT=1), CHIRP talking over the Bluetooth queue (PROGRAM_BtReceive in, BT_Send out), and the cable must
stay silent; "cable-bt" is that same build over the cable.
"""
import argparse
import ctypes
import glob
import os
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FLASH = 0x200000


def build(link):
    """One library per link, so each run starts from a fresh state (and Windows keeps a loaded DLL locked)."""
    bt = link != 'cable'
    out = os.path.join(ROOT, 'build', 'host')
    os.makedirs(out, exist_ok=True)
    dll = os.path.join(out, 'chirp_host_' + link.replace('-', '_') + ('.dll' if os.name == 'nt' else '.so'))
    src = os.path.join(ROOT, 'src')
    subprocess.run(['gcc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                    '-DPROGRAM_HOST_TEST', '-DCHANNELS_HOST_TEST', '-DSETTINGS_ENABLE_SAVE=1',
                    '-DPROGRAM_BT=%d' % bt, '-I', src,
                    os.path.join(ROOT, 'tests', 'chirp_host.c'), os.path.join(src, 'app', 'program.c'),
                    os.path.join(src, 'store', 'settings.c'), os.path.join(src, 'radio', 'tones.c'), '-o', dll],
                   check=True)
    return ctypes.CDLL(dll)


class Pipe:
    """What CHIRP's driver sees as the serial port."""
    port = 'TEST'

    def __init__(self, lib, bt):
        self.lib, self.timeout, self.buf = lib, 1.5, b''
        self.feed = lib.test_feed_bt if bt else lib.test_feed

    def write(self, data):
        self.feed(bytes(data), len(data))

    def read(self, n):
        end = time.monotonic() + self.timeout
        while len(self.buf) < n and time.monotonic() < end:
            b = ctypes.create_string_buffer(4096)
            got = self.lib.test_take(b, 4096)
            self.buf += b.raw[:got]
            if not got:
                time.sleep(0.001)
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def log(self, msg):
        pass


def run(drv, link):
    """The whole check list over one link; returns the names of the failed checks."""
    bt = link != 'cable'
    print('--- link: %s (%s build)' % (link, 'Bluetooth edition, PROGRAM_BT=1' if bt else 'open'))
    lib = build(link)
    lib.test_flash.restype = ctypes.POINTER(ctypes.c_uint8 * FLASH)
    flash = lib.test_flash().contents
    dumps = sorted(glob.glob(os.path.join(ROOT, 'firmware', 'backups', 'external-*.bin')))
    if dumps:
        ctypes.memmove(flash, open(dumps[-1], 'rb').read()[:FLASH], FLASH)
        print('flash: %s' % os.path.basename(dumps[-1]))
    else:
        ctypes.memset(flash, 0xFF, FLASH)
        assert lib.test_seed() == 0
        print('flash: empty, settings seeded')
    assert lib.test_load(), 'settings logs not readable'
    before = bytes(flash)

    stop = threading.Event()
    sessions = []

    def radio_loop():                                     # the app's main loop, as far as programming goes
        while not stop.is_set():
            r = lib.test_poll()
            if r:
                sessions.append(r)
            time.sleep(0.01)
    threading.Thread(target=radio_loop, daemon=True).start()

    def wait_session(n):
        end = time.monotonic() + 10
        while len(sessions) < n and time.monotonic() < end:
            time.sleep(0.05)
        assert len(sessions) >= n, 'session did not end'

    fails = []

    def check(name, ok, detail=''):
        print('%-48s %s %s' % (name, 'ok' if ok else 'FAILED', detail))
        if not ok:
            fails.append(name)

    def download():
        r = drv.UV5RMini(Pipe(lib, link == 'bt'))
        r.status_fn = lambda s: None
        r.sync_in()
        return r

    t = time.monotonic()
    r1 = download()
    img = r1.get_mmap().get_packed()
    wait_session(1)
    check('download (%.1f s)' % (time.monotonic() - t), len(img) == drv.UV5RMini.MEM_TOTAL)
    check('channels = flash 0x0000-0x7FFF', img[:0x8000] == before[:0x8000])
    check('DTMF = flash 0xA000-0xA1BF', img[0x8080:0x8240] == before[0xA000:0xA1C0])
    check('session that only read writes nothing', sessions[0] == 1 and bytes(flash) == before)
    m = r1.get_memory(1)
    print('  channel 1 as CHIRP reads it: %s %.5f MHz %s' % (m.name.strip() or '(no name)', m.freq / 1e6, m.mode))

    m = r1.get_memory(5)
    m.empty, m.freq, m.name, m.mode, m.duplex, m.offset = False, 145500000, 'CHIRP5', 'FM', '', 0
    r1.set_memory(m)
    up = r1.get_mmap().get_packed()
    e0 = lib.test_count(0)
    t = time.monotonic()
    r1.sync_out()
    wait_session(2)
    check('upload (%.1f s, %d erases)' % (time.monotonic() - t, lib.test_count(0) - e0), sessions[1] == 2)
    check('channels in flash = uploaded image', bytes(flash)[:0x8000] == up[:0x8000])
    check('DTMF in flash = uploaded image', bytes(flash)[0xA000:0xA1C0] == up[0x8080:0x8240])
    check('nothing at or past 0xB000 changed', bytes(flash)[0xB000:] == before[0xB000:])
    check('no refused or bad writes', lib.test_count(2) == 0 and lib.test_count(3) == 0)

    r2 = download()
    wait_session(3)
    back = r2.get_mmap().get_packed()
    diff = [i for i in range(len(up)) if up[i] != back[i]]
    check('second download = uploaded image', not diff, ('differs at %s' % ', '.join('0x%X' % i for i in diff[:8]))
          if diff else '')
    m2 = r2.get_memory(5)
    check('channel 5 reads back', m2.freq == 145500000 and m2.name.strip() == 'CHIRP5',
          '%s %.5f' % (m2.name.strip(), m2.freq / 1e6))

    e0 = lib.test_count(0)
    r2.sync_out()
    wait_session(4)
    check('unchanged upload erases nothing', lib.test_count(0) == e0, '%d erases' % (lib.test_count(0) - e0))
    if link == 'bt':
        check('nothing sent on the cable', lib.test_count(4) == 0, '%d UART sends' % lib.test_count(4))
    stop.set()
    return ['%s: %s' % (link, f) for f in fails]


def main():
    ap = argparse.ArgumentParser()
    here = [os.path.join(os.path.dirname(ROOT), 'chirp'), 'E:/chirp-ref/chirp']
    ap.add_argument('--chirp', default=next((p for p in here if os.path.isdir(p)), here[0]))
    ap.add_argument('--link', choices=['cable', 'bt', 'cable-bt', 'all'], default='all')
    a = ap.parse_args()
    if not os.path.isdir(os.path.join(a.chirp, 'chirp', 'drivers')):
        sys.exit('no CHIRP checkout at %s (git clone https://github.com/kk7ds/chirp)' % a.chirp)
    sys.path.insert(0, a.chirp)
    import builtins
    builtins._ = lambda s: s                              # CHIRP's gettext, as its app installs it
    from chirp import chirp_common, platform               # noqa: E402,F401
    from chirp.drivers import baofeng_uv17Pro as drv      # noqa: E402
    plat = platform.get_platform()
    plat.is_ble_serial = lambda s: False                  # a serial port (cable, or a Bluetooth SPP COM port)

    fails = []
    for link in (['cable', 'bt', 'cable-bt'] if a.link == 'all' else [a.link]):
        fails += run(drv, link)
    print('ALL OK' if not fails else 'FAILED: %s' % ', '.join(fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
