#!/usr/bin/env python3
"""Run the C update mode (app/update.c) on the PC against the real flasher host (tools/uv5r_flasher.py).

    python tests/update_host.py [--firmware build/open-uv5r-mini-nrf-v0.27.patch.json]

The C code is built into a DLL with the host gcc and driven through UPDATE_Port callbacks. The other end of the
emulated cable is flash_radio(), the same code that flashes real radios. When the C code hands over, the loader
stage continues in the flasher's emulated loader, so the whole update runs end to end, and the test checks:
  * the C code fetched the pinned loader byte for byte and passed it to store_loader
  * the hand-over record is the stock layout (type, result, magic, pins, baud, loader address, CRC)
  * the update finished with result 0 and the emulated flash equals the image
plus a bad-file case (no loader in ota.bin): the C code must refuse with result 0x83 and hand nothing over.
"""
import argparse
import ctypes
import hashlib
import os
import struct
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import uv5r_flasher as F  # noqa: E402

READ = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.POINTER(ctypes.c_uint8), ctypes.c_int, ctypes.c_uint32)
WRITE = ctypes.CFUNCTYPE(None, ctypes.POINTER(ctypes.c_uint8), ctypes.c_int)
BAUD = ctypes.CFUNCTYPE(None, ctypes.c_uint32)
STORE = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32,
                         ctypes.POINTER(ctypes.c_uint32))
HANDOVER = ctypes.CFUNCTYPE(None, ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32)


class Port(ctypes.Structure):
    _fields_ = [('read', READ), ('write', WRITE), ('set_baud', BAUD), ('store_loader', STORE),
                ('handover', HANDOVER)]


def build_dll():
    out = os.path.join(ROOT, 'build', 'host')
    os.makedirs(out, exist_ok=True)
    dll = os.path.join(out, 'update.dll')
    subprocess.run(['gcc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', '-shared', '-I', ROOT,
                    os.path.join(ROOT, 'app', 'update.c'), '-o', dll], check=True)
    return ctypes.CDLL(dll)


LOADER_ADDR = 0x7C000   # any address: the test only checks it is passed through


def run_case(lib, stock, image, expect_ok):
    radio = F.EmuRadio(stock)
    link = F.EmuLink.__new__(F.EmuLink)           # a cable without the emulated app: the C code is the app
    link.radio = radio
    link.baud = F.APP_BAUD
    link.to_radio, link.to_host = F._Wire(), F._Wire()
    radio.link = link
    radio.alive = True
    radio.log = lambda s: logs.append(s)
    state = {'baud': F.APP_BAUD, 'loader': None, 'record': None, 'result': None, 'done': False}
    logs = []

    pending = bytearray()

    def c_read(buf, mx, timeout):
        if not pending:
            pending.extend(link.to_radio.recv(state['baud'], min(timeout, 2000) / 1000.0))
        n = min(len(pending), mx)
        ctypes.memmove(buf, bytes(pending[:n]), n)
        del pending[:n]
        return n

    def c_write(buf, n):
        link.to_host.send(ctypes.string_at(buf, n), state['baud'])

    def c_baud(b):
        state['baud'] = b

    def c_store(p, n, addr):
        state['loader'] = ctypes.string_at(p, n)
        addr[0] = LOADER_ADDR
        return 0

    def c_handover(p, n):
        state['record'] = ctypes.string_at(p, n)
        # the bootloader runs the loader: continue in the flasher's emulated loader
        radio.baud = F.APP_BAUD
        radio._parser = F.FrameParser()
        radio._loader(resumed=False)
        state['done'] = True

    port = Port(READ(c_read), WRITE(c_write), BAUD(c_baud), STORE(c_store), HANDOVER(c_handover))

    def radio_thread():
        state['result'] = lib.UPDATE_Run(ctypes.byref(port))
    radio.thread = threading.Thread(target=radio_thread, daemon=True)
    radio.thread.start()
    ok, msg = F.flash_radio(link, image, lambda s: logs.append(s))
    time.sleep(0.2)
    return ok, msg, state, radio, logs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--stock', default=os.path.join(ROOT, 'firmware', 'stock-nrf-v0.27.ufw'))
    ap.add_argument('--firmware', default=os.path.join(ROOT, 'build', 'open-uv5r-mini-nrf-v0.27.patch.json'))
    a = ap.parse_args()
    lib = build_dll()
    lib.UPDATE_Run.argtypes = [ctypes.POINTER(Port)]
    stock = open(a.stock, 'rb').read()
    image, name, _ = F.load_firmware(a.stock, a.firmware)
    bad = 0

    def check(what, good, detail=''):
        nonlocal bad
        bad += not good
        print('%-44s %s %s' % (what, 'ok ' if good else 'BAD', detail))

    # 1. a good image, end to end
    ok, msg, st, radio, logs = run_case(lib, stock, image, True)
    _, f = F.ufw_files(image)
    oo, on, _ = f['ota.bin']
    ota = image[oo:oo + on]
    lo, ll = struct.unpack_from('<II', ota, 4)
    check('update finished (result 0)', ok, msg)
    check('C code fetched the pinned loader', st['loader'] == ota[lo:lo + ll]
          and hashlib.sha256(st['loader'] or b'').hexdigest() in F.PINNED_L2)
    rec = st['record'] or bytes(112)
    want = bytearray(112)
    struct.pack_into('<HHH', want, 2, 0x5A04, 0x5A01, 0x5441)
    struct.pack_into('<IIII', want, 40, 61, 62, 9600, 0)
    struct.pack_into('<I', want, 72, LOADER_ADDR)
    struct.pack_into('<H', want, 0, F.crc16(bytes(want[2:80])))
    check('hand-over record has the stock layout', rec == bytes(want), rec[:80].hex() if rec != bytes(want) else '')
    fo, fn, _ = f['flash.bin']
    same = radio.flash[F.AREA_START:F.FLASH_SOC + fn] == image[fo + F.AREA_START - F.FLASH_SOC:fo + fn]
    bok, app, _ = radio.boot_check()
    check('emulated flash equals the image, boots', same and bok, str(F.boot_info(app)) if app else '')

    # 2. an image whose ota.bin holds no loader (the stock .ufw as shipped): refused, nothing handed over
    ok2, msg2, st2, _, _ = run_case(lib, stock, stock, False)
    check('file without a loader refused (0x83)', not ok2 and st2['result'] == 0x83 and st2['record'] is None,
          msg2)
    print('ALL OK' if not bad else '%d FAILED' % bad)
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
