#!/usr/bin/env python3
r"""Test src/radio/rds.c on the PC: make a made-up FM baseband (MPX) with a 19 kHz pilot, RDS on 57 kHz (PI 2D09,
station name "KKZX", radio text), mono audio-like noise, stereo (38 kHz) content and white noise, at a sample rate a
little off the one the decoder is told, then run tests/rds_host.c on it and check what it decoded.

    python tests/rds_host.py [--snr-db 20] [--seconds 6]

The RDS signal is built from the public standard (IEC 62106): blocks with check word and offset, differential
coding, biphase symbols, DSB on 57 kHz locked to the pilot (at an arbitrary phase, as the FM block's filters
leave it). Needs numpy and a host gcc (MSYS2 mingw64 on Windows: C:\msys64\mingw64\bin on PATH).
"""
import argparse
import os
import subprocess
import sys

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
POLY = 0x5B9
OFFSETS = [0x0FC, 0x198, 0x168, 0x1B4]          # A B C D (2A groups use C)
PI = 0x2D09
PS = 'KKZX    '
RT = 'OPEN-UV5R-MINI RDS TEST'


def check(data16):
    reg = data16 << 10
    for i in range(25, 9, -1):
        if reg & (1 << i):
            reg ^= POLY << (i - 10)
    return reg & 0x3FF


PTY = 10                                         # COUNTRY in the RBDS list
PTYN = 'COUNTRY!'
AF = [(224 + 2) << 8 | 114, 124 << 8 | 124]     # 0A block C: "2 AFs follow", 98.9; 99.9, 99.9
CT = (2026, 11, 27, 7, 45, -16)                  # UTC 2026-11-27 07:45, local offset -8 h (in half hours)


def mjd(y, m, d):
    import datetime
    return (datetime.date(y, m, d) - datetime.date(1858, 11, 17)).days


def groups():
    rt = (RT + '\r').ljust(32)
    out = []
    flags = (1 << 10) | (PTY << 5)               # TP on, the programme type
    while True:
        for seg in range(4):                     # 0A: station name, AF, music, DI bit 0 (stereo) in segment 3
            b = (0 << 12) | flags | (1 << 3) | ((1 << 2) if seg == 3 else 0) | seg
            out.append((PI, b, AF[seg % 2], (ord(PS[2 * seg]) << 8) | ord(PS[2 * seg + 1])))
        y, mo, d, h, mi, off = CT                # 4A: clock time
        j = mjd(y, mo, d)
        out.append((PI, (4 << 12) | flags | (j >> 15), ((j & 0x7FFF) << 1) | (h >> 4),
                    ((h & 15) << 12) | (mi << 6) | ((1 << 5) if off < 0 else 0) | abs(off)))
        for seg in range(2):                     # 10A: programme type name
            out.append((PI, (10 << 12) | flags | seg, (ord(PTYN[4 * seg]) << 8) | ord(PTYN[4 * seg + 1]),
                        (ord(PTYN[4 * seg + 2]) << 8) | ord(PTYN[4 * seg + 3])))
        for seg in range(8):                     # 2A: radio text
            b = (2 << 12) | flags | seg
            c = (ord(rt[4 * seg]) << 8) | ord(rt[4 * seg + 1])
            d = (ord(rt[4 * seg + 2]) << 8) | ord(rt[4 * seg + 3])
            out.append((PI, b, c, d))
        yield from out
        out = []


def bits(nbits):
    res = []
    for g in groups():
        for j, w in enumerate(g):
            blk = (w << 10) | (check(w) ^ OFFSETS[j])
            res.extend((blk >> (25 - i)) & 1 for i in range(26))
        if len(res) >= nbits:
            return np.array(res[:nbits])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--snr-db', type=float, default=20.0, help='RDS power to noise power in the RDS band (+-2.4 kHz)')
    ap.add_argument('--seconds', type=float, default=6.0)
    a = ap.parse_args()

    fs_told = 599552                            # 1171 interrupts a second times 512, as measured on Sean's radio
    fs = fs_told * (1 + 300e-6)                  # the real rate is a little off
    n = int(a.seconds * fs)
    t = np.arange(n) / fs
    rng = np.random.default_rng(1)
    fp = 19000.0
    ph0 = 0.7
    pilot = 3600 * np.cos(2 * np.pi * fp * t + ph0)

    nb = int(a.seconds * fp / 16) + 2
    d = bits(nb)
    e = np.bitwise_xor.accumulate(np.concatenate([[0], d]))[1:]   # differential coding
    tb = (2 * np.pi * fp * t + ph0) / (2 * np.pi) / 16           # bit clock locked to the pilot
    k = np.floor(tb).astype(int)
    half = (tb - k) < 0.5
    sym = np.where(e[k] == 1, np.where(half, 1.0, -1.0), np.where(half, -1.0, 1.0))
    w = int(fs / 4750)                                            # soften the edges a little
    sym = np.convolve(sym, np.ones(w) / w, mode='same')
    rds_amp = 1600.0
    rds = rds_amp * sym * np.cos(3 * (2 * np.pi * fp * t + ph0) + 1.1)

    def band_noise(rms, hi):
        x = rng.standard_normal(n)
        X = np.fft.rfft(x)
        f = np.fft.rfftfreq(n, 1 / fs)
        X[f > hi] = 0
        x = np.fft.irfft(X, n)
        return x * rms / x.std()

    mono = band_noise(9000, 15000)
    stereo = band_noise(3000, 15000) * np.cos(2 * (2 * np.pi * fp * t + ph0))
    # white noise set so the RDS band (4.8 kHz wide) has the asked SNR; RDS power = rds_amp^2 / 2 (DSB) roughly
    p_rds = np.mean(rds ** 2)
    p_noise_band = p_rds / 10 ** (a.snr_db / 10)
    sigma = np.sqrt(p_noise_band * (fs / 2) / 4800)
    noise = rng.standard_normal(n) * sigma
    x = pilot + rds + mono + stereo + noise
    x = np.clip(np.round(x), -51000, 51000).astype('<i4')

    out = os.path.join(ROOT, 'build', 'host')
    os.makedirs(out, exist_ok=True)
    raw = os.path.join(out, 'rds_mpx.bin')
    x.tofile(raw)
    exe = os.path.join(out, 'rds_host' + ('.exe' if os.name == 'nt' else ''))
    subprocess.run(['gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-I', os.path.join(ROOT, 'src'),
                    os.path.join(ROOT, 'tests', 'rds_host.c'), os.path.join(ROOT, 'src', 'radio', 'rds.c'),
                    '-o', exe], check=True)
    r = subprocess.run([exe, raw, str(fs_told)], capture_output=True, text=True, check=True).stdout
    print('SNR %.1f dB in the RDS band, %.1f s:' % (a.snr_db, a.seconds))
    print(r.rstrip())
    ok = ('pi 2D09' in r) and ('ps [KKZX    ]' in r) and (RT in r) and ('ct 2026-11-27 07:45 off -16' in r) \
        and ('pty 10 COUNTRY tp 1 ms 1' in r) and ('ptyn [COUNTRY!]' in r) and ('af 124 114' in r) \
        and ('call KKZX' in r)
    print('PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
