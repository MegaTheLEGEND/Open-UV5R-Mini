#!/usr/bin/env python3
"""Decode RDS from a capture made on the radio (complex 16-bit I/Q, the 57 kHz band mixed down and decimated).

    python tools/rds_decode.py build/rds_989_iq.bin [--fs 4687.5]

Our own code, from the public RDS standard (IEC 62106 / NRSC-4): 1187.5 bit/s biphase BPSK on 57 kHz, differential
coding, 26-bit blocks (16 data bits + 10 check bits, generator x^10+x^8+x^7+x^5+x^4+x^3+1) with offset words A, B,
C, C', D. Prints the PI code, the programme service name (group 0A/0B) and radiotext (2A) when found.
"""
import argparse
import numpy as np

POLY = 0x5B9                                   # x^10 + x^8 + x^7 + x^5 + x^4 + x^3 + 1
OFFSETS = {'A': 0x0FC, 'B': 0x198, 'C': 0x168, "C'": 0x350, 'D': 0x1B4}


def syndrome(block26):
    reg = block26
    for i in range(25, 9, -1):
        if reg & (1 << i):
            reg ^= POLY << (i - 10)
    return reg & 0x3FF


SYN = {syndrome(o): name for name, o in OFFSETS.items()}   # syndrome of a block with no data bits = its offset's


def check_word(data16):
    reg = data16 << 10
    for i in range(25, 9, -1):
        if reg & (1 << i):
            reg ^= POLY << (i - 10)
    return reg & 0x3FF


def block_type(block26):
    data = block26 >> 10
    chk = (block26 & 0x3FF) ^ check_word(data)
    for name, o in OFFSETS.items():
        if chk == o:
            return name
    return None


def demod(z, fs):
    n = len(z)
    t = np.arange(n) / fs
    z2 = z * z
    # carrier: BPSK squared has a line at twice the offset; coarse FFT, then a fine search
    nf = 1 << 16
    Z = np.fft.fftshift(np.fft.fft(z2 * np.hanning(n), nf))
    f = np.fft.fftshift(np.fft.fftfreq(nf, 1 / fs))
    off = f[np.argmax(np.abs(Z) * (np.abs(f) < 600))] / 2
    fine = np.arange(off - 3, off + 3, 0.02)
    off = fine[np.argmax([abs(np.dot(z2, np.exp(-4j * np.pi * o * t))) for o in fine])]
    zr = z * np.exp(-2j * np.pi * off * t)
    # carrier phase, tracked in windows of about 0.1 s (it drifts with the two clocks)
    win = int(fs * 0.1)
    ph = np.zeros(n)
    centers, phases = [], []
    for a in range(0, n, win // 2):
        seg = zr[a:a + win]
        if len(seg) < win // 2:
            break
        centers.append(a + len(seg) / 2)
        phases.append(np.angle(np.mean(seg * seg)) / 2)
    phases = np.unwrap(np.array(phases) * 2) / 2
    ph = np.interp(np.arange(n), centers, phases)
    r = np.real(zr * np.exp(-1j * ph))
    return r, off, (57000 + off) / 48

def bits_from(r, fs, bitrate, up=16):
    from scipy.signal import resample_poly
    ru = resample_poly(r, up, 1)
    fsu = fs * up
    spb = fsu / bitrate                         # samples per bit after upsampling
    n = len(ru)
    h = int(round(spb / 2))
    pulse = np.concatenate([np.ones(h), -np.ones(h)])
    mf = np.convolve(ru, pulse[::-1], mode='same')   # matched filter for the biphase pulse
    best = None
    for phase in np.linspace(0, spb, 64, endpoint=False):
        idx = (phase + spb * np.arange(int((n - phase) / spb))).astype(int)
        idx = idx[idx < n]
        v = mf[idx]
        e = np.mean(np.abs(v)) / (np.std(np.abs(v)) + 1e-9)
        if best is None or e > best[0]:
            best = (e, v, phase)
    v = best[1]
    return (v > 0).astype(int), v / np.mean(np.abs(v))


def fix(block26):
    """the block's offset name, correcting up to 2 wrong bits within 5 of each other (RDS corrects bursts)"""
    t = block_type(block26)
    if t:
        return block26, t, 0
    for i in range(26):
        v = block26 ^ (1 << i)
        t = block_type(v)
        if t:
            return v, t, 1
    for i in range(26):
        for j in range(i + 1, min(26, i + 5)):
            v = block26 ^ (1 << i) ^ (1 << j)
            t = block_type(v)
            if t:
                return v, t, 2
    return block26, None, 0


def groups(bits):
    out = []
    n = len(bits)
    words = []
    for i in range(n - 26):
        w = 0
        for b in bits[i:i + 26]:
            w = (w << 1) | int(b)
        words.append(w)
    # sync: positions where an exact block is followed by exact blocks 26 and 52 bits on, in A,B,C/C',D order
    order = ['A', 'B', 'C', 'D']
    start = None
    for i in range(len(words) - 104):
        t0 = block_type(words[i])
        if not t0:
            continue
        k = order.index('C' if t0 == "C'" else t0)
        nxt = [block_type(words[i + 26 * j]) for j in (1, 2)]
        exp = [order[(k + j) % 4] for j in (1, 2)]
        if all(a and (a == e or (e == 'C' and a == "C'")) for a, e in zip(nxt, exp)):
            start = i - 26 * k
            break
    if start is None:
        return out, None
    while start < 0:
        start += 104
    i = start
    while i + 104 <= len(words):
        blk, types, nfix = [], [], 0
        for j in range(4):
            v, t, c = fix(words[i + 26 * j])
            blk.append(v >> 10)
            types.append(t)
            nfix += c
        out.append((i, blk, types, nfix))
        i += 104
    return out, start


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('file')
    ap.add_argument('--fs', type=float, default=600000.0 / 128)
    a = ap.parse_args()
    x = np.fromfile(a.file, dtype='<i2').astype(float)
    z = x[0::2] + 1j * x[1::2]
    z -= z.mean()
    r, off, bitrate = demod(z, a.fs)
    print('carrier offset %+.1f Hz, bit rate %.2f bit/s, %d samples (%.2f s)' % (off, bitrate, len(z), len(z) / a.fs))
    for inv in (0, 1):
        raw, conf = bits_from(r if not inv else -r, a.fs, bitrate)
        d = raw[1:] ^ raw[:-1]                  # differential decoding (sign of the carrier does not matter)
        g, sync = groups(d)
        blocks = 0
        w = [0] * len(d)
        for i in range(len(d) - 26):
            v = 0
            for b in d[i:i + 26]:
                v = (v << 1) | int(b)
            if block_type(v):
                blocks += 1
        print('pass %d: %d bits, %d exact blocks anywhere (about %d by chance), sync at %s' % (inv, len(d), blocks, len(d) * 5 // 1024, sync))
        ps = ['?'] * 8
        rt = {}
        pis = {}
        for pos, data, types, nfix in g:
            if types[0] != 'A' or types[1] != 'B':
                print('  group at bit %5d: blocks %s (lost)' % (pos, ' '.join(t or '-' for t in types)))
                continue
            pis[data[0]] = pis.get(data[0], 0) + 1
            gt = data[1] >> 11
            ver = (data[1] >> 10) & 1
            if gt == 0 and types[3] == 'D':
                seg = data[1] & 3
                ps[2 * seg] = chr(data[3] >> 8) if 32 <= data[3] >> 8 < 127 else '.'
                ps[2 * seg + 1] = chr(data[3] & 255) if 32 <= data[3] & 255 < 127 else '.'
            if gt == 2 and ver == 0 and types[2] in ('C',) and types[3] == 'D':
                seg = data[1] & 15
                for j, wd in enumerate((data[2], data[3])):
                    for k, ch in enumerate((wd >> 8, wd & 255)):
                        rt[4 * seg + 2 * j + k] = chr(ch) if 32 <= ch < 127 else '.'
            print('  group at bit %5d: PI %04X  type %d%s  blocks %s  bits fixed %d' % (pos, data[0], gt, 'AB'[ver], ' '.join(t or '-' for t in types), nfix))
        if pis:
            print('PI codes:', {('%04X' % k): v for k, v in pis.items()})
            print('PS name: "%s"' % ''.join(ps))
            if rt:
                print('radiotext:', ''.join(rt.get(i, '_') for i in range(max(rt) + 1)))


if __name__ == '__main__':
    main()
