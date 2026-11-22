#!/usr/bin/env python3
"""Open UV-5R Mini flasher: GUI (and CLI) for flashing base nrf-v0.27 radios over the USB programming cable,
with a built-in emulated radio to rehearse on first.

  python tools/uv5r_flasher.py                       # GUI
  python tools/uv5r_flasher.py --cli --stock firmware/stock-nrf-v0.27.ufw --firmware build/x.patch.json --port emu
  python tools/uv5r_flasher.py --cli ... --port COM5  # a real radio in update mode (asks to confirm)

Firmware may be a patch (.json, applied to the stock file) or a flashable .ufw. Before anything is sent, the image
must pass the same checks as tools/flashcheck.py (container, CRCs, pinned loader, area, stock bytes) and its app
must contain the stock update mode, so the radio can always be flashed again.

The update protocol is the stock one, as documented by Codeplug Studio
(pskillen/codeplug-studio docs/reference/radios/baofeng/uv-5r-mini/firmware.md). This is an independent
implementation: frames `AA 55 | len u16 | cmd u8 | data | crc16-xmodem u16`, len = 1 + data length; commands
1 rate/hello, 2 read {off u32, len u32} -> {off, len, bytes}, 3 result {code}, 4 length {u32}, 6 ping.
The radio drives the session; the host only answers reads from the image and echoes the radio's own frames.

The emulated radio ("emu" port) models that session end to end, with a 1 MiB emulated flash that starts as stock,
an erase of 0x4000..0x7C000, a loader record that survives a power cut (so a cut update resumes), and a boot check
that decrypts the installed app and verifies every CRC the way the bootloader would. It emulates the update
protocol and the flash, not the CPU: it does not run the firmware.
"""
import argparse
import hashlib
import json
import os
import queue
import struct
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from jl_ufw_unpack import jl_crypt  # noqa: E402
from mkufw import APP_AREA, APP_BIN_OFFSET, app_area_crypt, crc16  # noqa: E402
from ufwpatch import apply_patch  # noqa: E402

STOCK_SHA = '811b4b37253e0685c7efbd3ff0a24aac2bcad223e5453d95b5bfc3f42c089128'
FLASH_SOC = 0x1000
AREA_START, AREA_END = 0x4000, 0x7C000
WINDOWS = [(0x4100, 0x7BBC8), (0x4000, 0x4004), (0x4020, 0x4024)]
# SHA-256 of the packed uart_user.bin in ota.bin: layout-L2 loaders (nrf-v0.27 radios), from Codeplug Studio's pins.
PINNED_L2 = {'957bb486bb1d8cb5c971e366cd5c45a6da3c64fe611203096b56bb799ab02372': 'L2 (1.1.0)',
             '1e6c3e0d563a50f38caa2972c773ccdff99aa875c1e9098cc923be1cb5b6193e': 'L2-v2'}

# Roadmap 1.9: the cable update mode the image's app must keep, or the radio can only be recovered with the case open.
# Stock v0.27 app code that the update mode runs (entry, screen, UART, parser, record, reset, loader store, and the
# RAM code with the flash driver and hand-over), as app addresses. All of it byte for byte = the stock update mode.
APP_BASE = 0x01E00100
STOCK_UPDATE_CODE = [
    (0x01E492C0, 0x01E4948C),   # entry check and setup (PTT-A + =)
    (0x01E4917A, 0x01E491C2),   # UPDATE... screen
    (0x01E42AF2, 0x01E42D30),   # loader-slot allocator
    (0x01E42EE4, 0x01E42F6C),   # hand-over record
    (0x01E43206, 0x01E432E6),   # frame parser
    (0x01E431A2, 0x01E431EE),   # baud
    (0x01E433C8, 0x01E43426),   # send
    (0x01E43590, 0x01E43676),   # record filler, reset, uart_update task
    (0x01E59374, 0x01E59546),   # UART ISR
    (0x01E68F1A, 0x01E693FC),   # loader download and store
    (0x01E6AAAC, 0x01E6AAAC + 53492),  # RAM code: flash driver, hand-over
]
# Our own C update mode declares itself with this marker and the flash windows its code may write
# (src/update/update_radio.c). Allowed: the app area and the update loader's slot; never the rest of PRCT or VM.
OUR_MARKER = b'OPENUV5R-UPDATE-MODE/1 writes='
ALLOWED_WRITES = [(0x1000, 0x7C000), (0xFA000, 0xFE000)]
RESCUE_WARNING = (
    "WARNING: this image's cable update mode has not been proven on a radio. If it does not work, this radio can NOT "
    "be flashed again over the cable. The only way back is to open the case and use the ROM USB rescue mode: wires "
    "soldered to the G, M and P pads, a USB key sender (an ESP32 with tools/jl_usbkey) and a full backup of this "
    "radio's flash made beforehand (docs/TESTING.md, roadmap 1.8). Without all of that, do not continue.")
CONFIRM_PHRASE = 'I CAN OPEN THE CASE'


def update_mode_status(app, stock_app):
    """('stock' | 'ours' | 'none', detail). 'ours' still needs the rescue warning: not proven on a radio."""
    if not app:
        return 'none', 'no app'
    if stock_app and all(hi - APP_BASE <= len(app)
                         and app[lo - APP_BASE:hi - APP_BASE] == stock_app[lo - APP_BASE:hi - APP_BASE]
                         for lo, hi in STOCK_UPDATE_CODE):
        return 'stock', 'the stock update mode, unchanged'
    i = app.find(OUR_MARKER)
    if i >= 0:
        end = app.find(b';', i)
        spec = app[i + len(OUR_MARKER):end if end > i else i + 80].decode('latin1')
        try:
            wins = [tuple(int(x, 16) for x in w.split('-')) for w in spec.split(',') if w]
        except ValueError:
            return 'none', 'our marker is unreadable'
        bad = [w for w in wins if len(w) != 2 or not any(lo <= w[0] < w[1] <= hi for lo, hi in ALLOWED_WRITES)]
        if not wins or bad:
            return 'none', 'its update mode may write outside the app and loader slot: %s' % spec
        return 'ours', 'Open-UV5R update mode (writes %s); not proven on a radio' % spec
    return 'none', 'MISSING: a radio running this could not be flashed again over the cable'


def needs_rescue_warning(checks):
    return any(c == 'Update mode' and ok and d.startswith('Open-UV5R') for c, ok, d in checks)


CMD_BAUD, CMD_DATA, CMD_RESULT, CMD_LENGTH, CMD_PING = 1, 2, 3, 4, 6
APP_BAUD, LOADER_BAUD = 9600, 38400
MAX_READ = 0x1000
T_LISTEN, T_PING, T_PING_TOTAL, T_SETTLE, T_IDLE = 2.0, 1.0, 30.0, 0.3, 60.0


# ----------------------------------------------------------------------------------------------------------- frames
def frame(cmd, data=b''):
    body = b'\xAA\x55' + struct.pack('<HB', 1 + len(data), cmd) + bytes(data)
    return body + struct.pack('<H', crc16(body))


class FrameParser:
    """Bytes in, (cmd, data) out. Junk before AA 55 is dropped; a frame with a bad CRC is returned as cmd None."""

    def __init__(self):
        self.buf = bytearray()

    def feed(self, b):
        self.buf += b
        out = []
        while True:
            i = self.buf.find(b'\xAA\x55')
            if i < 0:
                self.buf = self.buf[-1:] if self.buf[-1:] == b'\xAA' else bytearray()
                return out
            del self.buf[:i]
            if len(self.buf) < 4:
                return out
            ln = struct.unpack_from('<H', self.buf, 2)[0]
            if ln == 0:
                del self.buf[:2]
                out.append((None, b''))
                continue
            total = ln + 6
            if len(self.buf) < total:
                return out
            f = bytes(self.buf[:total])
            del self.buf[:total]
            if struct.unpack_from('<H', f, total - 2)[0] != crc16(f[:total - 2]):
                out.append((None, f))
            else:
                out.append((f[4], f[5:total - 2]))


# ------------------------------------------------------------------------------------------------------ image checks
def ufw_files(u):
    hdr = jl_crypt(u[:0x40])
    out = {}
    for i in range(struct.unpack_from('<H', hdr, 8)[0]):
        r = jl_crypt(u[0x40 + 0x50 * i:0x90 + 0x50 * i])
        name = r[0x40:].split(b'\0')[0].decode('latin1')
        off, ln = struct.unpack_from('<II', r, 8)
        out[name] = (off, ln, struct.unpack_from('<H', r, 4)[0])
    return hdr, out


def area_report(flash):
    """Decrypt the app area of flash.bin bytes (flash[0] = SoC 0x1000) and check its CRCs, as the bootloader does.
    Returns (ok, app bytes or None, list of problems)."""
    bad = []
    try:
        area_head = app_area_crypt(bytes(flash[APP_AREA:APP_AREA + 64]), 0)
        head, ent = area_head[:32], area_head[32:64]
        if head[16:29] != b'app_area_head' or ent[16:23] != b'app.bin':
            return False, None, ['no app area directory (erased or damaged)']
        area_len = struct.unpack_from('<I', head, 8)[0]
        app_len = struct.unpack_from('<I', ent, 8)[0]
        if not (APP_BIN_OFFSET <= area_len <= len(flash) - APP_AREA and APP_BIN_OFFSET + app_len <= area_len):
            return False, None, ['app area lengths out of range']
        n = (area_len + 31) // 32 * 32
        area = app_area_crypt(bytes(flash[APP_AREA:APP_AREA + n]), 0)
        app = area[APP_BIN_OFFSET:APP_BIN_OFFSET + app_len]
        if crc16(head[2:32]) != struct.unpack_from('<H', head, 0)[0]:
            bad.append('app_area_head entry CRC')
        if crc16(area[0x20:area_len]) != struct.unpack_from('<H', head, 2)[0]:
            bad.append('app area data CRC')
        if crc16(ent[2:32]) != struct.unpack_from('<H', ent, 0)[0]:
            bad.append('app.bin entry CRC')
        if crc16(app) != struct.unpack_from('<H', ent, 2)[0]:
            bad.append('app.bin data CRC')
        return not bad, app, bad
    except (struct.error, IndexError) as e:
        return False, None, ['unreadable app area: %s' % e]


def check_image(img, stock):
    """[(check, ok, detail)] for a flashable image against the stock .ufw. Same rules as tools/flashcheck.py."""
    res = []
    try:
        hdr, f = ufw_files(img)
        _, fs = ufw_files(stock)
    except Exception as e:  # noqa: BLE001
        return [('Container', False, 'not a .ufw: %s' % e)]
    res.append(('Container', set(f) == {'flash.bin', 'ota.bin'} and all(o + n <= len(img) for o, n, _ in f.values()),
                ', '.join(sorted(f))))
    if not res[-1][1]:
        return res
    crc_ok = (crc16(hdr[2:0x40]) == struct.unpack_from('<H', hdr, 0)[0]
              and crc16(img[0x40:0x40 + 0x50 * len(f)]) == struct.unpack_from('<H', hdr, 2)[0]
              and all(crc16(img[o:o + n]) == c for o, n, c in f.values()))
    fo, fl_n, _ = f['flash.bin']
    fl = img[fo:fo + fl_n]
    fh = jl_crypt(fl[:32])
    crc_ok &= crc16(fh[2:32]) == struct.unpack_from('<H', fh, 0)[0]
    a_ok, app, a_bad = area_report(fl)
    res.append(('CRCs', crc_ok and a_ok, 'all good' if crc_ok and a_ok else ', '.join(a_bad) or 'container CRC'))
    oo, on, _ = f['ota.bin']
    ota = img[oo:oo + on]
    ent = ota[:0x20]
    lo, ll = struct.unpack_from('<II', ent, 4)
    lsha = hashlib.sha256(ota[lo:lo + ll]).hexdigest()
    res.append(('Loader', ent[16:29] == b'uart_user.bin' and lsha in PINNED_L2,
                PINNED_L2.get(lsha, 'not a pinned nrf-v0.27 (L2) loader: %s' % lsha[:16])))
    res.append(('Area', FLASH_SOC + len(fl) <= AREA_END, 'flash.bin SoC 0x%X..0x%X' % (FLASH_SOC, FLASH_SOC + len(fl))))
    so, sn, _ = fs['flash.bin']
    sfl = stock[so:so + sn]
    out = [i for i in range(min(len(fl), len(sfl))) if fl[i] != sfl[i]
           and not any(lo_ <= FLASH_SOC + i < hi for lo_, hi in WINDOWS)]
    res.append(('Stock bytes', len(fl) == len(sfl) and not out,
                'only the app window differs' if not out else 'differs at SoC 0x%X' % (FLASH_SOC + out[0])))
    _, sapp, _ = area_report(sfl)
    kind, detail = update_mode_status(app, sapp)
    res.append(('Update mode', kind != 'none', detail))
    return res


def load_firmware(stock_path, fw_path):
    """(image bytes, name, checks). Raises ValueError with a readable reason."""
    stock = open(stock_path, 'rb').read()
    if hashlib.sha256(stock).hexdigest() != STOCK_SHA:
        raise ValueError('the stock file is not stock-nrf-v0.27.ufw (only base nrf-v0.27 is supported)')
    if fw_path.lower().endswith('.json'):
        p = json.load(open(fw_path, encoding='utf-8'))
        try:
            img = apply_patch(p, stock)
        except SystemExit as e:
            raise ValueError(str(e))
        if p.get('base') != 'nrf-v0.27':
            raise ValueError('patch is for base %r, not nrf-v0.27' % p.get('base'))
        name = p.get('build', {}).get('name') or os.path.basename(fw_path)
    else:
        img = open(fw_path, 'rb').read()
        name = os.path.basename(fw_path)
    return img, name, check_image(img, stock)


def boot_info(app):
    """What the radio would show: the hold-8 start-up string and the version, read from the app's strings."""
    info = {}
    for s in (b'5RMINI', b'OPENUV'):
        i = app.find(s)
        if i >= 0:
            info['start-up'] = app[i:i + 16].split(b'\0')[0].decode('latin1')
            break
    i = app.find(b'V0.')
    if i >= 0:
        info['version'] = app[i:i + 8].split(b'\0')[0].decode('latin1')
    return info


# -------------------------------------------------------------------------------------------------------------- links
class SerialLink:
    """A real serial port (or a pyserial URL such as socket://127.0.0.1:PORT for a TCP serial bridge)."""

    def __init__(self, port):
        import serial
        self.s = serial.serial_for_url(port, baudrate=APP_BAUD, timeout=0.05)
        for attr in ('dtr', 'rts'):
            try:
                setattr(self.s, attr, False)
            except Exception:  # noqa: BLE001
                pass

    def read(self, timeout):
        self.s.timeout = timeout
        return self.s.read(4096)

    def write(self, b):
        self.s.write(b)
        self.s.flush()

    def set_baud(self, baud):
        self.s.baudrate = baud
        for attr in ('dtr', 'rts'):
            try:
                setattr(self.s, attr, False)
            except Exception:  # noqa: BLE001
                pass

    def close(self):
        self.s.close()


class _Wire:
    """One direction of the emulated cable. Bytes sent at a rate the receiver isn't set to are lost, as on a UART."""

    def __init__(self):
        self.q = queue.Queue()

    def send(self, b, baud):
        self.q.put((bytes(b), baud))

    def recv(self, baud, timeout):
        out = bytearray()
        end = time.monotonic() + timeout
        while True:
            try:
                b, at = self.q.get(timeout=max(0.0, end - time.monotonic()) if not out else 0)
            except queue.Empty:
                return bytes(out)
            if at == baud:
                out += b


class EmuLink:
    """Host end of the cable to an emulated radio."""

    def __init__(self, radio):
        self.radio = radio
        self.baud = APP_BAUD
        self.to_radio, self.to_host = _Wire(), _Wire()
        radio.plug(self)

    def read(self, timeout):
        return self.to_host.recv(self.baud, timeout)

    def write(self, b):
        self.to_radio.send(b, self.baud)

    def set_baud(self, baud):
        self.baud = baud

    def radio_off(self):
        """True once the emulated radio has stopped (power cut, refusal, or finished): no need to wait 60 s."""
        return not self.radio.thread.is_alive() and self.to_host.q.empty()

    def close(self):
        self.radio.unplug()


# ----------------------------------------------------------------------------------------------------- emulated radio
class EmuRadio:
    """An nrf-v0.27 radio as the update protocol sees it: stock app update stage, then the L2 loader.

    flash: 1 MiB, SoC addresses. record: the loader's progress record (survives a power cut, like the real one).
    """

    def __init__(self, stock_ufw):
        self.flash = bytearray(b'\xFF' * 0x100000)
        _, f = ufw_files(stock_ufw)
        o, n, _ = f['flash.bin']
        self.flash[FLASH_SOC:FLASH_SOC + n] = stock_ufw[o:o + n]
        self.record = None          # {'area': n, 'done': bytes written} while an update is unfinished
        self.cut_at = None          # fraction of the loader write after which the power is cut (test)
        self.log = lambda s: None
        self.link = None
        self.thread = None
        self.baud = APP_BAUD
        self.keys_update = True     # power on holding PTT-A and = (update mode)

    # -- the radio's view of the cable
    def plug(self, link):
        self.link = link
        self.alive = True
        self.thread = threading.Thread(target=self._power_on, daemon=True)
        self.thread.start()

    def unplug(self):
        self.alive = False

    def _send(self, cmd, data=b''):
        self.link.to_host.send(frame(cmd, data), self.baud)

    def _recv(self, timeout):
        """Next good frame (cmd, data) or None."""
        end = time.monotonic() + timeout
        while self.alive and time.monotonic() < end:
            b = self.link.to_radio.recv(self.baud, min(0.05, max(0.0, end - time.monotonic())))
            if b:
                for cmd, data in self._parser.feed(b):
                    if cmd is not None:
                        return cmd, data
        return None

    def _read(self, off, n):
        """Ask the host for image bytes, retrying a few times."""
        for _ in range(3):
            self._send(CMD_DATA, struct.pack('<II', off, n))
            end = time.monotonic() + 3
            while time.monotonic() < end:
                r = self._recv(end - time.monotonic())
                if r is None:
                    break
                cmd, d = r
                if cmd == CMD_DATA and d[:8] == struct.pack('<II', off, n) and len(d) == 8 + n:
                    return d[8:]
        raise IOError('host stopped answering reads')

    # -- power on
    def boot_check(self):
        """What the bootloader would find in flash: (ok, app, problems)."""
        return area_report(self.flash[FLASH_SOC:AREA_END])

    def _power_on(self):
        self._parser = FrameParser()
        self.baud = APP_BAUD
        try:
            if self.record is not None:
                self.log('radio: dark screen, the loader resumes an unfinished update')
                self._loader(resumed=True)
                return
            ok, app, bad = self.boot_check()
            if not ok:
                self.log('radio: app area damaged (%s): bricked, needs a case-open repair' % ', '.join(bad))
                return
            if not self.keys_update:
                self.log('radio: normal start-up (%s)' % boot_info(app))
                return
            if b'UPDATE\x00' not in app:
                self.log('radio: the installed app has no update mode, keys ignored: needs a case-open repair')
                return
            self.log('radio: UPDATE... (stock app update mode)')
            self._app_stage()
        except IOError as e:
            self.log('radio: %s' % e)
        except Exception as e:  # noqa: BLE001
            self.log('radio: emulator error %r' % e)

    def _app_stage(self):
        while self.alive:                                   # wait for the ping
            r = self._recv(60)
            if r is None:
                return
            if r[0] == CMD_PING:
                self._send(CMD_BAUD)                        # pong
                break
        r = self._recv(5)                                   # the host asks for a rate
        if not r or r[0] != CMD_BAUD or len(r[1]) < 4:
            return
        self.baud = struct.unpack('<I', r[1][:4])[0]
        # Read the container like the stock app does: header, records, then the loader.
        hdr = jl_crypt(self._read(0, 0x40))
        if crc16(hdr[2:0x40]) != struct.unpack_from('<H', hdr, 0)[0] or hdr[0x10:0x16] != b'AC695N':
            self._send(CMD_RESULT, b'\x83')
            return
        nrec = struct.unpack_from('<H', hdr, 8)[0]
        recs = self._read(0x40, 0x50 * nrec)
        ota = None
        for i in range(nrec):
            rr = jl_crypt(recs[0x50 * i:0x50 * i + 0x50])
            if rr[0x40:].split(b'\0')[0] == b'ota.bin':
                ota = struct.unpack_from('<II', rr, 8)
        if not ota:
            self._send(CMD_RESULT, b'\x83')
            return
        ent = self._read(ota[0], 0x20)
        lo, ll = struct.unpack_from('<II', ent, 4)
        if ent[16:29] != b'uart_user.bin':
            self._send(CMD_RESULT, b'\x83')
            return
        loader = b''.join(self._read(ota[0] + lo + i, min(0x200, ll - i)) for i in range(0, ll, 0x200))
        if hashlib.sha256(loader).hexdigest() not in PINNED_L2:
            self._send(CMD_RESULT, b'\x8F')
            return
        self.log('radio: loader received (%d bytes), resetting into it' % ll)
        self._loader(resumed=False)

    def _loader(self, resumed):
        self.baud = APP_BAUD
        while self.alive:                                   # hello at 9600 until told the rate
            self._send(CMD_BAUD, struct.pack('<I', APP_BAUD))
            r = self._recv(0.5)
            if r and r[0] == CMD_BAUD and len(r[1]) >= 4:
                break
        else:
            return
        self.baud = struct.unpack('<I', r[1][:4])[0]
        end = time.monotonic() + 0.7                        # hello again at the new rate, ~700 ms
        while self.alive:
            self._send(CMD_BAUD, struct.pack('<I', self.baud))
            r = self._recv(0.2)
            if r and r[0] == CMD_BAUD:
                break
            if time.monotonic() > end:
                self.log('radio: no answer at %d, loader resets to 9600' % self.baud)
                return self._loader(resumed)
        hdr = jl_crypt(self._read(0, 0x40))
        nrec = struct.unpack_from('<H', hdr, 8)[0]
        recs = self._read(0x40, 0x50 * nrec)
        fl = None
        for i in range(nrec):
            rr = jl_crypt(recs[0x50 * i:0x50 * i + 0x50])
            if rr[0x40:].split(b'\0')[0] == b'flash.bin':
                fl = struct.unpack_from('<II', rr, 8)
        if not fl:
            self._send(CMD_RESULT, b'\x8F')
            return
        f_off, f_len = fl
        if FLASH_SOC + f_len > AREA_END:
            self._send(CMD_RESULT, b'\x90')
            return
        area = FLASH_SOC + f_len - AREA_START
        self._send(CMD_LENGTH, struct.pack('<I', area))
        r = self._recv(3)
        if not r or r[0] != CMD_LENGTH:
            return
        if self.record is None:
            self.log('radio: erasing 0x%X..0x%X' % (AREA_START, AREA_START + area))
            self.flash[AREA_START:AREA_START + area] = b'\xFF' * area
            self.record = {'area': area, 'done': 0}
        else:
            self.log('radio: continuing from 0x%X' % (AREA_START + self.record['done']))
        cut = None if self.cut_at is None else int(area * self.cut_at) // MAX_READ * MAX_READ
        while self.record['done'] < area:
            if cut is not None and self.record['done'] >= cut:
                self.cut_at = None
                self.alive = False
                self.log('radio: POWER CUT at 0x%X (test)' % (AREA_START + self.record['done']))
                return
            pos = self.record['done']
            n = min(MAX_READ, area - pos)
            data = self._read(f_off + (AREA_START - FLASH_SOC) + pos, n)
            self.flash[AREA_START + pos:AREA_START + pos + n] = data
            self.record['done'] = pos + n
        self.record = None
        self._send(CMD_RESULT, b'\x00')
        self._recv(2)                                       # the host echoes the result
        self.log('radio: update finished, restarting')


# ------------------------------------------------------------------------------------------------------------ updater
RESULT_TEXT = {
    0x00: 'Done. The radio restarts by itself; check its power-on screen.',
    0x01: 'Nothing was written: the image is not for this radio\'s key. Switch the radio off and on.',
    0x83: 'The radio refused the file (not for this firmware).',
    0x86: 'This firmware is already on the radio, or it can\'t be replaced by this image. Nothing was erased.',
    0x87: 'Nothing was written; the radio boots the installed app.',
    0x8A: 'Nothing was written: the image is for another stock version.',
    0x8F: 'The file isn\'t for this radio\'s flash layout; the radio refused it. Nothing installed.',
    0x90: 'The image runs past the area the radio can install. Nothing was erased.',
}
RECOVER = ('The update did not finish. Leave the cable in, switch the radio off and on with NO keys held '
           '(the screen stays dark), then press Flash again with the same files: the loader continues.')


class Cancelled(Exception):
    pass


def flash_radio(link, image, log, progress=lambda p: None, cancel=threading.Event()):
    """Run one update session. Returns (ok, message)."""
    _, f = ufw_files(image)
    f_off, f_len = f['flash.bin'][:2]
    area_lo = f_off + (AREA_START - FLASH_SOC)
    parser = FrameParser()
    rate = APP_BAUD
    phase = 'listen'
    reads = 0
    area_len = None
    result = None
    touched = False

    def send(cmd, data=b''):
        if cmd == CMD_DATA:                                 # outgoing guard: only image bytes the radio asked for
            off, n = struct.unpack_from('<II', data)
            if n > MAX_READ or off + n > len(image) or data[8:] != image[off:off + n]:
                raise ValueError('refusing to send: read 0x%X+0x%X is outside the image' % (off, n))
        link.write(frame(cmd, data))

    def frames(timeout):
        if cancel.is_set():
            raise Cancelled()
        out = []
        for cmd, d in parser.feed(link.read(timeout)):
            if cmd is None:
                log('  (bad frame ignored)')
            else:
                out.append((cmd, d))
        return out

    log('Listening %.0f s for a loader that is part-way through an update...' % T_LISTEN)
    pending = []
    end = time.monotonic() + T_LISTEN
    while time.monotonic() < end and not pending:
        pending = [x for x in frames(0.1) if x[0] == CMD_BAUD]
    if pending:
        log('The radio was part-way through an update: continuing it.')
        phase, touched = 'loader', True
        send(CMD_BAUD, struct.pack('<I', LOADER_BAUD))
        link.set_baud(LOADER_BAUD)
        rate = LOADER_BAUD
        pending = []
    else:
        log('Pinging the radio (hold PTT-A and = while switching on, release at UPDATE...)')
        phase = 'ping'
        end = time.monotonic() + T_PING_TOTAL
        while phase == 'ping':
            if time.monotonic() > end:
                return False, 'No radio in update mode answered. Nothing was sent that the radio acted on.'
            send(CMD_PING)
            t = time.monotonic() + T_PING
            while time.monotonic() < t:
                if any(c == CMD_BAUD and not d for c, d in frames(0.1)):
                    phase = 'app'
                    break
        settle = time.monotonic() + T_SETTLE                # soak up pongs to earlier pings
        while time.monotonic() < settle:
            frames(0.05)
        log('Radio in update mode answered. App stage at 9600 (the radio is not changed yet).')
        send(CMD_BAUD, struct.pack('<I', APP_BAUD))

    last = time.monotonic()
    while True:
        got = pending or frames(0.25)
        pending = []
        if not got:
            if time.monotonic() - last > T_IDLE or getattr(link, 'radio_off', lambda: False)():
                return False, ('The radio stopped talking. ' + RECOVER) if touched else 'The radio stopped talking.'
            continue
        last = time.monotonic()
        for cmd, d in got:
            if cmd == CMD_BAUD:
                if phase == 'app' and reads:
                    log('Hand-over: the radio reset into the loader. Switching to 38400.')
                    phase, touched = 'loader', True
                    send(CMD_BAUD, struct.pack('<I', LOADER_BAUD))
                    link.set_baud(LOADER_BAUD)
                    rate = LOADER_BAUD
                elif phase == 'loader':
                    if rate != LOADER_BAUD:
                        send(CMD_BAUD, struct.pack('<I', LOADER_BAUD))
                        link.set_baud(LOADER_BAUD)
                        rate = LOADER_BAUD
                    else:
                        send(CMD_BAUD, struct.pack('<I', LOADER_BAUD))
            elif cmd == CMD_DATA and len(d) >= 8:
                off, n = struct.unpack_from('<II', d)
                send(CMD_DATA, d[:8] + image[off:off + n])
                reads += 1
                if phase == 'loader' and area_len and off >= area_lo:
                    progress(min(1.0, (off + n - area_lo) / area_len))
            elif cmd == CMD_LENGTH and phase == 'loader' and len(d) >= 4:
                area_len = struct.unpack('<I', d[:4])[0]
                log('The radio is erasing its firmware area (0x%X bytes) and writing...' % area_len)
                send(CMD_LENGTH, d)
            elif cmd == CMD_RESULT and d:
                result = d[0]
                if phase == 'loader':
                    send(CMD_RESULT, d)
                txt = RESULT_TEXT.get(result, 'Result 0x%02X.' % result)
                if result != 0 and phase == 'loader' and area_len:
                    txt += ' ' + RECOVER
                progress(1.0 if result == 0 else 0)
                return result == 0, txt
            elif cmd == 0x7F and len(d) >= 36:
                v = struct.unpack_from('<7I', d)          # our update mode's debug frame (src/update/update_radio.c)
                log('  radio: step %d, last asked 0x%X (retries %d), received %d bytes, sent %d frames, dropped %d,'
                    ' reads answered %d, collects flag/backstop %d/%d, last in: %s' % (v[1], v[2] & 0xFFFFFF, v[2] >> 24, v[3], v[4], v[5], reads, v[0] >> 16, v[0] & 0xFFFF,
                                                         d[28:36].hex(' ')))
            else:
                log('  (frame cmd %d ignored)' % cmd)


# ---------------------------------------------------------------------------------------------------------------- CLI
def cli(a):
    log = print
    img, name, checks = load_firmware(a.stock, a.firmware)
    for c, ok, d in checks:
        print('%-12s %s %s' % (c, 'ok ' if ok else 'BAD', d))
    if not all(ok for _, ok, _ in checks):
        raise SystemExit('FAILED: not flashing')
    if a.port == 'emu':
        radio = EmuRadio(open(a.stock, 'rb').read())
        radio.log = log
        if a.cut is not None:
            radio.cut_at = a.cut
        ok, msg = flash_radio(EmuLink(radio), img, log, lambda p: None)
        print(msg)
        if not ok and a.cut is not None:
            print('-- power on again with no keys (resume) --')
            time.sleep(0.2)
            ok, msg = flash_radio(EmuLink(radio), img, log)
            print(msg)
        time.sleep(0.2)
        bok, app, bad = radio.boot_check()
        _, f = ufw_files(img)
        o, n, _ = f['flash.bin']
        same = radio.flash[AREA_START:FLASH_SOC + n] == img[o + AREA_START - FLASH_SOC:o + n]
        print('emulated flash equals the image: %s; boot check: %s %s' % (
            same, 'ok' if bok else 'BAD ' + ', '.join(bad), boot_info(app) if app else ''))
        sys.exit(0 if ok and bok and same else 1)
    if needs_rescue_warning(checks):
        print('\n' + '!' * 78 + '\n' + RESCUE_WARNING + '\n' + '!' * 78)
        if input('To go on anyway, type %s: ' % CONFIRM_PHRASE).strip() != CONFIRM_PHRASE:
            raise SystemExit('not flashed')
    if input('Flash %s to the radio on %s? Type yes: ' % (name, a.port)).strip() != 'yes':
        raise SystemExit('not flashed')
    link = SerialLink(a.port)
    try:
        ok, msg = flash_radio(link, img, log, lambda p: print('\r%3d%%' % (p * 100), end=''))
    finally:
        link.close()
    print(msg)
    sys.exit(0 if ok else 1)


# ---------------------------------------------------------------------------------------------------------------- GUI
def gui():
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk

    root = tk.Tk()
    root.title('Open UV-5R Mini Flasher')
    root.minsize(720, 520)
    ev = queue.Queue()
    state = {'radio': None, 'busy': False, 'cancel': threading.Event()}
    default_stock = os.path.join(os.path.dirname(HERE), 'firmware', 'stock-nrf-v0.27.ufw')
    EMU = 'Emulated radio (built in)'

    v_stock = tk.StringVar(value=default_stock if os.path.exists(default_stock) else '')
    v_fw = tk.StringVar()
    v_port = tk.StringVar(value=EMU)
    v_cut = tk.BooleanVar(value=False)
    v_cutpct = tk.IntVar(value=40)
    v_status = tk.StringVar(value='Pick the stock file and a firmware (patch .json or flashable .ufw).')

    frm = ttk.Frame(root, padding=10)
    frm.pack(fill='both', expand=True)
    frm.columnconfigure(1, weight=1)

    def picker(row, label, var, types):
        ttk.Label(frm, text=label).grid(row=row, column=0, sticky='w', pady=2)
        ttk.Entry(frm, textvariable=var).grid(row=row, column=1, sticky='ew', padx=6)
        ttk.Button(frm, text='Browse...', command=lambda: var.set(
            filedialog.askopenfilename(filetypes=types) or var.get())).grid(row=row, column=2)

    picker(0, 'Stock v0.27 .ufw', v_stock, [('UFW', '*.ufw'), ('All', '*.*')])
    picker(1, 'Firmware', v_fw, [('Patch or UFW', '*.json *.ufw'), ('All', '*.*')])

    ttk.Label(frm, text='Radio').grid(row=2, column=0, sticky='w', pady=2)
    port_box = ttk.Combobox(frm, textvariable=v_port, state='readonly')
    port_box.grid(row=2, column=1, sticky='ew', padx=6)

    def refresh_ports():
        ports = [EMU]
        try:
            from serial.tools import list_ports
            ports += ['%s  %s' % (p.device, p.description) for p in list_ports.comports()]
        except Exception:  # noqa: BLE001
            pass
        port_box['values'] = ports
    ttk.Button(frm, text='Refresh', command=refresh_ports).grid(row=2, column=2)
    refresh_ports()

    emu = ttk.LabelFrame(frm, text='Emulated radio', padding=6)
    emu.grid(row=3, column=0, columnspan=3, sticky='ew', pady=6)
    ttk.Checkbutton(emu, text='Cut the power during the write at', variable=v_cut).pack(side='left')
    ttk.Spinbox(emu, from_=1, to=99, width=4, textvariable=v_cutpct).pack(side='left')
    ttk.Label(emu, text='%   ').pack(side='left')

    checks = ttk.Treeview(frm, columns=('ok', 'detail'), height=6)
    checks.heading('#0', text='Check')
    checks.heading('ok', text='')
    checks.heading('detail', text='Detail')
    checks.column('#0', width=110, stretch=False)
    checks.column('ok', width=40, stretch=False, anchor='center')
    checks.grid(row=4, column=0, columnspan=3, sticky='ew')

    bar = ttk.Progressbar(frm, maximum=1000)
    bar.grid(row=5, column=0, columnspan=3, sticky='ew', pady=6)
    btns = ttk.Frame(frm)
    btns.grid(row=6, column=0, columnspan=3, sticky='ew')
    logw = tk.Text(frm, height=12, wrap='word', state='disabled')
    logw.grid(row=7, column=0, columnspan=3, sticky='nsew', pady=(6, 0))
    frm.rowconfigure(7, weight=1)
    ttk.Label(frm, textvariable=v_status, wraplength=700).grid(row=8, column=0, columnspan=3, sticky='w', pady=4)

    def log(s):
        ev.put(('log', s))

    def show_checks(res):
        checks.delete(*checks.get_children())
        for c, ok, d in res:
            checks.insert('', 'end', text=c, values=('ok' if ok else 'BAD', d))

    def get_image():
        try:
            img, name, res = load_firmware(v_stock.get(), v_fw.get())
        except (OSError, ValueError, KeyError, json.JSONDecodeError) as e:
            messagebox.showerror('Firmware', str(e))
            return None
        show_checks(res)
        state['checks'] = res
        if not all(ok for _, ok, _ in res):
            v_status.set('Checks FAILED: this image will not be flashed.')
            return None
        v_status.set('All checks passed for %s.' % name)
        return img, name

    def emu_radio():
        if state['radio'] is None:
            state['radio'] = EmuRadio(open(v_stock.get(), 'rb').read())
            state['radio'].log = log
            log('Emulated radio created with stock v0.27 in its flash.')
        return state['radio']

    def run_bg(fn):
        if state['busy']:
            return
        state['busy'] = True
        state['cancel'].clear()

        def w():
            try:
                fn()
            except Cancelled:
                log('Stopped.')
            except Exception as e:  # noqa: BLE001
                log('Error: %s' % e)
            finally:
                ev.put(('done', None))
        threading.Thread(target=w, daemon=True).start()

    def do_flash():
        got = get_image()
        if not got:
            return
        img, name = got
        port = v_port.get()
        if port == EMU:
            radio = emu_radio()
            radio.keys_update = True
            radio.cut_at = v_cutpct.get() / 100 if v_cut.get() else None
            link = EmuLink(radio)
        else:
            dev = port.split()[0]
            if needs_rescue_warning(state.get('checks', [])) and not messagebox.askyesno(
                    'Not recoverable over the cable', RESCUE_WARNING + '\n\nContinue anyway?', icon='warning',
                    default='no'):
                return
            if not messagebox.askyesno('Flash a real radio', (
                    'Flash "%s" to the radio on %s?\n\n'
                    'Charge the battery first. Put the radio in update mode: switch off, hold PTT-A and =, switch on, '
                    'release at UPDATE..., then press Yes. Keep the cable in until it restarts.') % (name, dev),
                    icon='warning'):
                return
            try:
                link = SerialLink(dev)
            except Exception as e:  # noqa: BLE001
                messagebox.showerror('Port', str(e))
                return
        bar['value'] = 0

        def job():
            try:
                ok, msg = flash_radio(link, img, log, lambda p: ev.put(('progress', p)), state['cancel'])
            finally:
                link.close()
            log(msg)
            ev.put(('status', ('Finished: ' if ok else 'Not finished: ') + msg))
            if port == EMU:
                time.sleep(0.3)
                emu_boot()
        run_bg(job)

    def emu_boot():
        r = emu_radio()
        ok, app, bad = r.boot_check()
        log('Emulated radio boot check: %s%s' % ('ok ' + str(boot_info(app)) if ok else 'FAILED (%s)' % ', '.join(bad),
                                                ' | update unfinished: power on with no keys to resume'
                                                if r.record else ''))

    def emu_reset():
        state['radio'] = None
        emu_radio()

    ttk.Button(btns, text='Check image', command=get_image).pack(side='left')
    ttk.Button(btns, text='Flash', command=do_flash).pack(side='left', padx=6)
    ttk.Button(btns, text='Stop', command=lambda: state['cancel'].set()).pack(side='left')
    ttk.Separator(btns, orient='vertical').pack(side='left', fill='y', padx=10)
    ttk.Button(btns, text='Emu: boot check', command=emu_boot).pack(side='left')
    ttk.Button(btns, text='Emu: back to stock', command=emu_reset).pack(side='left', padx=6)

    def pump():
        try:
            while True:
                k, v = ev.get_nowait()
                if k == 'log':
                    logw.configure(state='normal')
                    logw.insert('end', v + '\n')
                    logw.see('end')
                    logw.configure(state='disabled')
                elif k == 'progress':
                    bar['value'] = v * 1000
                elif k == 'status':
                    v_status.set(v)
                elif k == 'done':
                    state['busy'] = False
        except queue.Empty:
            pass
        root.after(50, pump)
    pump()
    root.mainloop()


def main():
    ap = argparse.ArgumentParser(description='Open UV-5R Mini flasher (GUI without --cli)')
    ap.add_argument('--cli', action='store_true')
    ap.add_argument('--stock', default=os.path.join(os.path.dirname(HERE), 'firmware', 'stock-nrf-v0.27.ufw'))
    ap.add_argument('--firmware')
    ap.add_argument('--port', default='emu', help='emu (built-in emulated radio), COMn, or a pyserial URL')
    ap.add_argument('--cut', type=float, help='emu only: cut the power at this fraction of the write, then resume')
    ap.add_argument('--loader-baud', type=int, default=LOADER_BAUD,
                    help='rate asked of the loader (default 38400; 9600 keeps the rate the app stage used)')
    a = ap.parse_args()
    globals()['LOADER_BAUD'] = a.loader_baud
    if a.cli:
        if not a.firmware:
            ap.error('--firmware is required with --cli')
        cli(a)
    else:
        gui()


if __name__ == '__main__':
    main()
