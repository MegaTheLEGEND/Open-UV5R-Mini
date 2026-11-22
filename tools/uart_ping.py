#!/usr/bin/env python3
"""Ping a radio in update mode over the cable and show what comes back. Sends nothing else: no data, no rate change.

The update mode (src/update/update.c) answers a ping (cmd 6) with an empty cmd 1 frame. Frames are
`AA 55 | len u16 | cmd u8 | data | crc16-xmodem u16` (tools/uv5r_flasher.py).

    python tools/uart_ping.py --port COM5                 # 9600 baud, as the flasher starts
    python tools/uart_ping.py --port COM5 --scan          # also try other rates (if the radio's clock isn't 48 MHz)

With --scan, a clean answer at another rate tells how fast the radio's UART clock really is: clock = 48 MHz * rate / 9600.
"""
import argparse
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkufw import crc16  # noqa: E402

try:
    import serial
except ImportError:
    sys.exit('needs pyserial: pip install pyserial')

CMD_PING = 6
RATES = [9600, 4800, 2400, 1200, 19200, 38400, 7200, 14400, 3600]


def frame(cmd, data=b''):
    body = b'\xAA\x55' + struct.pack('<HB', 1 + len(data), cmd) + data
    return body + struct.pack('<H', crc16(body))


def find_reply(buf):
    """Return the first well-formed frame (cmd, data) in buf, or None."""
    i = buf.find(b'\xAA\x55')
    while i >= 0 and i + 7 <= len(buf):
        n = struct.unpack_from('<H', buf, i + 2)[0]
        end = i + 4 + n + 2
        if 1 <= n <= 0x400 and end <= len(buf):
            f = buf[i:end]
            if struct.unpack_from('<H', f, len(f) - 2)[0] == crc16(f[:-2]):
                return f[4], f[5:-2]
        i = buf.find(b'\xAA\x55', i + 1)
    return None


def try_rate(port, rate, tries):
    with serial.Serial(port, rate, timeout=0.3) as s:
        s.reset_input_buffer()
        got = b''
        for _ in range(tries):
            s.write(frame(CMD_PING))
            time.sleep(0.4)
            got += s.read(256)
            r = find_reply(got)
            if r:
                return True, got, r
        return False, got, None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--port', required=True, help='the K1 cable, e.g. COM5')
    ap.add_argument('--scan', action='store_true', help='also try other baud rates')
    ap.add_argument('--tries', type=int, default=5)
    args = ap.parse_args()

    for rate in (RATES if args.scan else RATES[:1]):
        ok, raw, reply = try_rate(args.port, rate, args.tries)
        shown = raw[:48].hex(' ') + (' ...' if len(raw) > 48 else '')
        if ok:
            print('%6d baud: ANSWER cmd %d, %d data bytes   raw: %s' % (rate, reply[0], len(reply[1]), shown))
            print('the radio answers at %d baud: its UART clock is about %.1f MHz' % (rate, 48.0 * rate / 9600))
            return 0
        print('%6d baud: %s' % (rate, ('no answer, but got: ' + shown) if raw else 'nothing at all'))
    print('no clean answer.')
    return 1


if __name__ == '__main__':
    sys.exit(main())
