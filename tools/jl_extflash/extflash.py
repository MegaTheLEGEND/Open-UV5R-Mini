#!/usr/bin/env python3
"""Read the radio's external SPI flash (GT25Q16A, 2 MB) through the AC695N's ROM USB download mode.

The radio must be in UBOOT1.00 mode (tools/jl_usbkey), freshly: no loader run since it was switched on. This
script puts extflash.bin (built from extflash.c) in RAM at 0x12000, then for each 4 KB asks it to read the
external flash into RAM at 0x13000 and fetches that with the ROM's memory-read command. Nothing is written to
any flash. The whole chip is read twice and the two reads are compared.

Needs kagaimiq's jl-uboot-tool (for the USB protocol), next to this repo by default. Windows: run as admin.

  python tools/jl_extflash/extflash.py --device \\\\.\\PHYSICALDRIVE3 -o firmware/backups/external.bin
"""
import argparse
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

PROG_ADDR = 0x12000
PARAMS = 0x12F00
BUF = 0x13000
CHUNK = 0x1000
MAGIC = 0x45584653
DONE = 0x600DF00D
CMD_ID, CMD_READ, CMD_COPY = 1, 2, 3


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--device', required=True, help=r'the BR23 UBOOT1.00 disk, e.g. \\.\PHYSICALDRIVE3')
    ap.add_argument('--jltool', default=os.path.join(os.path.dirname(ROOT), 'jl-uboot-tool'))
    ap.add_argument('--prog', default=os.path.join(ROOT, 'build', 'extflash', 'extflash.bin'))
    ap.add_argument('-o', '--out', required=True, help='output file (a second read goes to OUT.2)')
    ap.add_argument('--id-only', action='store_true', help='only read the JEDEC ID')
    ap.add_argument('--copy', nargs=2, metavar=('ADDR', 'LEN'), type=lambda s: int(s, 0),
                    help='instead: copy memory ADDR..ADDR+LEN to OUT with CPU loads (e.g. the ROM)')
    args = ap.parse_args()

    sys.path.insert(0, args.jltool)
    from jltech.uboot import JL_MSCDevice, JL_UBOOT
    from jltech.cipher import cipher_bytes, jl_crc_cipher

    prog = open(args.prog, 'rb').read()

    with JL_MSCDevice(args.device) as dev:
        vendor, product, rev = dev.inquiry()
        print('device: %s %s %s' % (vendor, product, rev))
        if vendor.lower() != 'br23' or product != 'UBOOT1.00':
            sys.exit('not a BR23 in UBOOT1.00 mode; switch the radio off and enter the mode again')
        rom = JL_UBOOT(dev.dev)

        # Which way does this ROM's memory read/write scramble data? Find out with a test pattern in the buffer.
        pattern = bytes((i * 7 + 3) & 0xFF for i in range(256))
        mode = None
        for wcrypt in (True, False):
            rom.mem_write(BUF, cipher_bytes(jl_crc_cipher, pattern) if wcrypt else pattern)
            back = rom.mem_read(BUF, 256)
            for rcrypt in (False, True):
                if (cipher_bytes(jl_crc_cipher, back) if rcrypt else back) == pattern:
                    mode = (wcrypt, rcrypt)
                    break
            if mode:
                break
        if not mode:
            sys.exit('memory read-back test failed; stopping')
        wcrypt, rcrypt = mode
        print('memory test ok (write scrambled: %s, read scrambled: %s)' % mode)

        def mwrite(addr, data):
            rom.mem_write(addr, cipher_bytes(jl_crc_cipher, data) if wcrypt else data)

        def mread(addr, n):
            out = b''
            while n > 0:
                k = min(n, 256)
                d = rom.mem_read(addr, k)
                out += cipher_bytes(jl_crc_cipher, d) if rcrypt else d
                addr += k
                n -= k
            return out

        for off in range(0, len(prog), 256):
            mwrite(PROG_ADDR + off, prog[off:off + 256])
        if mread(PROG_ADDR, len(prog)) != prog:
            sys.exit('program upload did not read back the same; stopping')
        print('program loaded at 0x%X (%d bytes)' % (PROG_ADDR, len(prog)))

        def call(cmd, addr=0, length=0):
            mwrite(PARAMS, struct.pack('<6I', MAGIC, cmd, addr, length, 0, 0))
            rom.mem_jump(PROG_ADDR, 0)
            magic, _, _, _, done, result = struct.unpack('<6I', mread(PARAMS, 24))
            if done != DONE:
                sys.exit('the program did not finish (done=0x%08X); stopping' % done)
            return result

        if args.copy:
            start, length = args.copy
            data = bytearray()
            for addr in range(start, start + length, CHUNK):
                if call(CMD_COPY, addr, CHUNK) != CHUNK:
                    sys.exit('copy failed at 0x%X' % addr)
                data += mread(BUF, CHUNK)
            open(args.out, 'wb').write(data[:length])
            print('copied 0x%X bytes from 0x%08X -> %s (%d%% 0xFF, %d%% 0x00)' % (
                length, start, args.out, data.count(0xFF) * 100 // len(data), data.count(0) * 100 // len(data)))
            return

        jedec = call(CMD_ID)
        print('external flash JEDEC ID: %06X' % jedec)
        if jedec in (0, 0xFFFFFF):
            sys.exit('no answer from the external flash (wrong pins, or it has no power in this mode)')
        size = 1 << (jedec & 0xFF)
        if not 0x10000 <= size <= 0x2000000:
            sys.exit('odd size code in the ID; stopping')
        print('size from the ID: %d bytes' % size)
        if args.id_only:
            return

        reads = []
        for n in (1, 2):
            path = args.out if n == 1 else args.out + '.2'
            data = bytearray()
            t0 = time.time()
            for addr in range(0, size, CHUNK):
                got = call(CMD_READ, addr, CHUNK)
                if got != CHUNK:
                    sys.exit('short read at 0x%X' % addr)
                data += mread(BUF, CHUNK)
                if addr % 0x40000 == 0:
                    print('  read %d: 0x%06X' % (n, addr), flush=True)
            open(path, 'wb').write(data)
            print('read %d done in %.0f s -> %s' % (n, time.time() - t0, path))
            reads.append(bytes(data))
        if reads[0] == reads[1]:
            print('both reads identical')
        else:
            diff = sum(a != b for a, b in zip(*reads))
            print('READS DIFFER in %d bytes: the read is not reliable' % diff)


if __name__ == '__main__':
    main()
