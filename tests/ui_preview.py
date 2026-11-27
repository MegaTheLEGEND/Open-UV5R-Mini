#!/usr/bin/env python3
r"""Build and run tests/ui_preview.c (src/ui/screen_main.c with a stand-in LCD) on the PC, and write the screens it
draws as PNGs (4x size) into build/ui/, to look at before anything goes on a radio.

  python tests/ui_preview.py
  python tests/ui_preview.py --screenshots     # also copy them to docs/screenshots/ (README pictures)
"""
import os
import struct
import subprocess
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'src')
OUT = os.path.join(ROOT, 'build', 'ui')
SCALE = 4


def ppm_to_png(ppm, png):
    data = open(ppm, 'rb').read()
    parts = data.split(b'\n', 3)
    w, h = map(int, parts[1].split())
    px = parts[3]
    rows = []
    for y in range(h):
        row = px[y * w * 3:(y + 1) * w * 3]
        big = b''.join(row[x * 3:x * 3 + 3] * SCALE for x in range(w))
        rows.extend([b'\x00' + big] * SCALE)

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(png, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w * SCALE, h * SCALE, 8, 2, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(b''.join(rows), 9)) + chunk(b'IEND', b''))


def main():
    os.makedirs(OUT, exist_ok=True)
    exe = os.path.join(OUT, 'ui_preview.exe')
    subprocess.run(['gcc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
                    '-I', os.path.join(ROOT, 'tests', 'stubs'), '-I', SRC,
                    os.path.join(ROOT, 'tests', 'ui_preview.c'), os.path.join(SRC, 'ui', 'screen_main.c'), os.path.join(SRC, 'ui', 'screen_menu.c'),
                    os.path.join(SRC, 'ui', 'screen_apps.c'), os.path.join(SRC, 'ui', 'status.c'),
                    os.path.join(SRC, 'ui', 'helper.c'), os.path.join(SRC, 'ui', 'font.c'),
                    os.path.join(SRC, 'ui', 'icons.c'), '-o', exe], check=True)
    r = subprocess.run([exe, OUT])
    for fn in sorted(os.listdir(OUT)):
        if fn.endswith('.ppm'):
            ppm_to_png(os.path.join(OUT, fn), os.path.join(OUT, fn[:-4] + '.png'))
            os.remove(os.path.join(OUT, fn))
    print('previews in %s' % os.path.relpath(OUT, ROOT))
    if '--screenshots' in sys.argv[1:] and r.returncode == 0:
        shots = os.path.join(ROOT, 'docs', 'screenshots')
        os.makedirs(shots, exist_ok=True)
        for fn in sorted(os.listdir(OUT)):
            if fn.endswith('.png'):
                with open(os.path.join(OUT, fn), 'rb') as a, open(os.path.join(shots, fn), 'wb') as b:
                    b.write(a.read())
        print('screenshots in %s' % os.path.relpath(shots, ROOT))
    return r.returncode


if __name__ == '__main__':
    sys.exit(main())
