#!/usr/bin/env python3
"""Read memory (RAM, SFRs or ROM) from the AC695N in its ROM USB download mode (UBOOT1.00). Read only.

The radio must be freshly in the mode (tools/jl_usbkey) with no loader run. Windows: run as administrator.

  python tools/jl_memdump.py --device \\\\.\\PHYSICALDRIVE3 0x02000000 0x4000 -o build/rom-02000000.bin

The ROM's memory read scrambles its answer with JieLi's CrcDecode cipher, one read at a time; this checks that
on a known pattern first (as tools/jl_extflash/extflash.py does) and undoes it.
"""
import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEST = 0x13000


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--device', required=True)
    ap.add_argument('--jltool', default=os.path.join(os.path.dirname(ROOT), 'jl-uboot-tool'))
    ap.add_argument('addr', type=lambda s: int(s, 0))
    ap.add_argument('length', type=lambda s: int(s, 0))
    ap.add_argument('-o', '--out', required=True)
    a = ap.parse_args()

    sys.path.insert(0, a.jltool)
    from jltech.uboot import JL_MSCDevice, JL_UBOOT
    from jltech.cipher import cipher_bytes, jl_crc_cipher

    with JL_MSCDevice(a.device) as dev:
        vendor, product, _ = dev.inquiry()
        print('device: %s %s' % (vendor, product))
        if vendor.lower() != 'br23' or product != 'UBOOT1.00':
            sys.exit('not a BR23 in UBOOT1.00 mode')
        rom = JL_UBOOT(dev.dev)

        pattern = bytes((i * 7 + 3) & 0xFF for i in range(256))
        mode = None
        for wcrypt in (True, False):
            rom.mem_write(TEST, cipher_bytes(jl_crc_cipher, pattern) if wcrypt else pattern)
            back = rom.mem_read(TEST, 256)
            for rcrypt in (False, True):
                if (cipher_bytes(jl_crc_cipher, back) if rcrypt else back) == pattern:
                    mode = rcrypt
                    break
            if mode is not None:
                break
        if mode is None:
            sys.exit('memory read-back test failed')
        print('memory test ok (read scrambled: %s)' % mode)

        out = bytearray()
        addr, left = a.addr, a.length
        while left:
            k = min(left, 256)
            d = rom.mem_read(addr, k)
            out += cipher_bytes(jl_crc_cipher, d) if mode else d
            addr += k
            left -= k
        open(a.out, 'wb').write(out)
        print('read 0x%X bytes from 0x%08X -> %s (%d%% 0xFF, %d%% 0x00)' % (
            len(out), a.addr, a.out, out.count(0xFF) * 100 // len(out), out.count(0) * 100 // len(out)))


if __name__ == '__main__':
    main()
