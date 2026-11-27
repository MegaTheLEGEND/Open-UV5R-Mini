#!/usr/bin/env python3
"""The PC side of the radio's remote-control monitor (src/app/monitor.h; ENABLE_DEBUGGING builds).

One process owns the cable's serial port and logs everything the radio says (build/radiomon.log, with times);
commands come in on a local TCP port, one line each, and the reply ('@' lines up to "@ok" / "@err") goes back.
Unasked '#' lines (events: APRS packets heard, FSK sync words) only go to the log.

    python tools/radiomon.py serve --port COM7            # keep running (in the background)
    python tools/radiomon.py send "state"                 # a command ('!' added); prints the reply
    python tools/radiomon.py send "app stat"
    python tools/radiomon.py log [-n 40]                  # the end of the log

tools/radio_mcp.py offers the same as MCP tools for an AI agent. CHIRP still works on the same cable while this runs
only if this is stopped first (one program per serial port).
"""
import argparse
import os
import queue
import socket
import socketserver
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG = os.path.join(ROOT, 'build', 'radiomon.log')
HOST, TCP_PORT = '127.0.0.1', 8765


def stamp():
    t = time.time()
    return time.strftime('%H:%M:%S', time.localtime(t)) + '.%03d' % (int(t * 1000) % 1000)


class Radio:
    def __init__(self, port, baud):
        import serial
        self.serial_mod = serial
        self.port, self.baud = port, baud
        self.ser = None
        self.replies = queue.Queue()
        self.lock = threading.Lock()
        self.log = open(LOG, 'a', encoding='utf-8', buffering=1)
        threading.Thread(target=self.reader, daemon=True).start()

    def note(self, text):
        self.log.write('%s %s\n' % (stamp(), text))

    def open(self):
        while True:
            try:
                self.ser = self.serial_mod.Serial(self.port, self.baud, timeout=0.1)
                self.note('--- port %s open' % self.port)
                return
            except (self.serial_mod.SerialException, OSError) as e:
                self.note('--- port error: %s' % e)
                time.sleep(2)

    def reader(self):
        buf = b''
        while True:
            if self.ser is None:
                self.open()
            try:
                b = self.ser.read(256)
            except (self.serial_mod.SerialException, OSError) as e:
                self.note('--- read error: %s' % e)
                self.ser = None
                time.sleep(1)
                continue
            buf += b
            while b'\n' in buf:
                line, buf = buf.split(b'\n', 1)
                text = line.decode('latin-1').rstrip('\r')
                if not text:
                    continue
                self.note(text)
                if text.startswith('@'):
                    self.replies.put(text)

    def command(self, cmd, timeout=4.0):
        cmd = cmd.strip()
        if not cmd.startswith('!'):
            cmd = '!' + cmd
        with self.lock:
            while not self.replies.empty():
                self.replies.get_nowait()
            if self.ser is None:
                return ['@err port not open']
            self.note('> ' + cmd)
            try:
                self.ser.write(b'\r\n' + cmd.encode('latin-1') + b'\r\n')
            except (self.serial_mod.SerialException, OSError) as e:
                return ['@err write: %s' % e]
            out, end = [], time.time() + timeout
            while time.time() < end:
                try:
                    r = self.replies.get(timeout=0.1)
                except queue.Empty:
                    continue
                out.append(r)
                if r == '@ok' or r.startswith('@err'):
                    return out
            out.append('@err no reply in %.0f s (radio not in a debug build, busy, or not on this port)' % timeout)
            return out


def serve(a):
    os.makedirs(os.path.dirname(LOG), exist_ok=True)
    radio = Radio(a.port, a.baud)

    class H(socketserver.StreamRequestHandler):
        def handle(self):
            line = self.rfile.readline().decode('latin-1').strip()
            if not line:
                return
            t = 4.0
            if line.startswith('~'):              # "~SECONDS command": a longer wait (beacon, big dumps)
                t, line = line[1:].split(' ', 1)
                t = float(t)
            self.wfile.write(('\n'.join(radio.command(line, t)) + '\n').encode('latin-1'))

    socketserver.ThreadingTCPServer.allow_reuse_address = True
    with socketserver.ThreadingTCPServer((HOST, TCP_PORT), H) as srv:
        radio.note('--- serving on %s:%d' % (HOST, TCP_PORT))
        srv.serve_forever()


def send(cmd, timeout=4.0):
    with socket.create_connection((HOST, TCP_PORT), timeout=timeout + 3) as s:
        s.sendall(('~%g %s\n' % (timeout, cmd)).encode('latin-1'))
        data = b''
        while True:
            b = s.recv(4096)
            if not b:
                break
            data += b
    return data.decode('latin-1').rstrip('\n')


def tail(n):
    try:
        with open(LOG, encoding='utf-8', errors='replace') as f:
            return ''.join(f.readlines()[-n:]).rstrip('\n')
    except FileNotFoundError:
        return '(no log yet)'


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='what', required=True)
    s = sub.add_parser('serve')
    s.add_argument('--port', default='COM7')
    s.add_argument('--baud', type=int, default=115200)
    c = sub.add_parser('send')
    c.add_argument('cmd')
    c.add_argument('--timeout', type=float, default=4.0)
    lg = sub.add_parser('log')
    lg.add_argument('-n', type=int, default=40)
    a = ap.parse_args()
    if a.what == 'serve':
        serve(a)
    elif a.what == 'send':
        try:
            print(send(a.cmd, a.timeout))
        except OSError as e:
            print('not running? (python tools/radiomon.py serve --port COM7): %s' % e)
            return 1
    else:
        print(tail(a.n))
    return 0


if __name__ == '__main__':
    sys.exit(main())
