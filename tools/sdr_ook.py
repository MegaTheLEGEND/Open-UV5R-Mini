#!/usr/bin/env python3
r"""Record an OOK remote with SDRangel (its REST API, default 127.0.0.1:8091) and find its pulses and code, to compare
with the radio's own capture (MENU -> APPS -> SUB-GHZ). Needs numpy, and SDRangel running with an RTL-SDR (or any
receiver) on device set 0 and a File Sink channel (index --chan) whose file name is set.

    python tools/sdr_ook.py record --seconds 4           # starts the File Sink, waits, stops; then analyses it
    python tools/sdr_ook.py analyse build/sdr/remote_x.sdriq

The analysis: the strongest frequency (FFT of the loudest samples), mixed to 0 and low-passed, the envelope against a
threshold half way (in dB) between the noise and the pulses, durations in us written next to the file (.txt, signed
like a Flipper RAW .sub), then src/radio/ook.c on them (built from tests/ook_cli.c with the host gcc).
"""
import argparse
import glob
import json
import os
import struct
import subprocess
import sys
import time
import urllib.request

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
API = 'http://127.0.0.1:8091/sdrangel'


def api(path, body=None, method=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(API + path, data=data, method=method or ('POST' if data else 'GET'),
                                 headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=5) as r:
        return json.loads(r.read() or b'{}')


def record(a):
    before = set(glob.glob(os.path.join(ROOT, 'build', 'sdr', '*.sdriq')))
    act = {'channelType': 'FileSink', 'direction': 0, 'FileSinkActions': {'record': 1}}
    api('/deviceset/0/channel/%d/actions' % a.chan, act)
    print('recording %g s: press the remote now' % a.seconds, flush=True)
    time.sleep(a.seconds)
    act['FileSinkActions']['record'] = 0
    api('/deviceset/0/channel/%d/actions' % a.chan, act)
    time.sleep(0.5)
    new = sorted(set(glob.glob(os.path.join(ROOT, 'build', 'sdr', '*.sdriq'))) - before, key=os.path.getmtime)
    if not new:
        new = sorted(glob.glob(os.path.join(ROOT, 'build', 'sdr', '*.sdriq')), key=os.path.getmtime)[-1:]
    if not new:
        print('no recording found in build/sdr')
        return 1
    return analyse(new[-1])


def read_sdriq(path):
    with open(path, 'rb') as f:
        hdr = f.read(32)
        rate, centre, start, size = struct.unpack('<IQQI', hdr[:24])
        raw = f.read()
    if size == 16:
        x = np.frombuffer(raw[:len(raw) // 4 * 4], dtype='<i2').astype(np.float32)
    else:
        x = np.frombuffer(raw[:len(raw) // 8 * 8], dtype='<i4').astype(np.float32)
    return rate, centre, x[0::2] + 1j * x[1::2]


def analyse(path):
    rate, centre, iq = read_sdriq(path)
    n = len(iq)
    print('%s: %d samples at %d S/s (%.2f s), centre %.6f MHz' % (os.path.basename(path), n, rate, n / rate,
                                                                    centre / 1e6))
    if n < rate // 10:
        print('too short')
        return 1
    p = np.abs(iq) ** 2
    loud = iq[p > np.percentile(p, 99)]
    nfft = 1 << 14
    spec = np.zeros(nfft)
    for k in range(0, len(loud) - nfft, nfft // 2):
        spec += np.abs(np.fft.fft(loud[k:k + nfft] * np.hanning(nfft))) ** 2
    if not spec.any():
        spec = np.abs(np.fft.fft(iq[:nfft] * np.hanning(nfft))) ** 2
    f = np.fft.fftfreq(nfft, 1 / rate)
    off = f[np.argmax(spec)]
    print('strongest at %+.1f kHz: %.4f MHz' % (off / 1e3, (centre + off) / 1e6))
    t = np.arange(n) / rate
    bb = iq * np.exp(-2j * np.pi * off * t)
    w = max(1, int(rate * 15e-6))                    # 15 us average: keeps 100 us pulses
    env = np.convolve(np.abs(bb) ** 2, np.ones(w) / w, mode='same')
    db = 10 * np.log10(env + 1e-9)
    noise, top = np.percentile(db, 30), np.percentile(db, 99.5)
    thr = (noise + top) / 2
    print('noise %.1f dB, pulses %.1f dB, threshold %.1f dB' % (noise, top, thr))
    if top - noise < 10:
        print('no signal (the loudest is under 10 dB above the noise)')
        return 1
    on = db > thr
    edges = np.flatnonzero(np.diff(on.astype(np.int8))) + 1
    if len(edges) < 4:
        print('no pulses')
        return 1
    durs = []
    for a, b in zip(edges[:-1], edges[1:]):
        us = int(round((b - a) * 1e6 / rate))
        durs.append(us if on[a] else -us)
    out = os.path.splitext(path)[0] + '.txt'
    with open(out, 'w') as fo:
        fo.write(' '.join(map(str, durs)) + '\n')
    pos = [d for d in durs if d > 0]
    print('%d durations -> %s; pulses %d..%d us (median %d)' % (len(durs), os.path.relpath(out, ROOT), min(pos),
                                                                max(pos), int(np.median(pos))))
    exe = os.path.join(ROOT, 'build', 'host', 'ook_cli' + ('.exe' if os.name == 'nt' else ''))
    os.makedirs(os.path.dirname(exe), exist_ok=True)
    subprocess.run(['gcc', '-std=c11', '-O2', '-I', os.path.join(ROOT, 'src'), os.path.join(ROOT, 'tests', 'ook_cli.c'),
                    os.path.join(ROOT, 'src', 'radio', 'ook.c'), '-o', exe], check=True)
    print(subprocess.run([exe, out], capture_output=True, text=True).stdout.strip())
    return 0


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='what', required=True)
    r = sub.add_parser('record')
    r.add_argument('--seconds', type=float, default=4)
    r.add_argument('--chan', type=int, default=1)
    an = sub.add_parser('analyse')
    an.add_argument('file')
    a = ap.parse_args()
    return record(a) if a.what == 'record' else analyse(a.file)


if __name__ == '__main__':
    sys.exit(main())
