#!/usr/bin/env python3
"""Prepares the files for System Update on the device (Menu > System > System Update).

  python tools/make_update.py [outdir]           default: build/sd_update

Writes opgbc_fpga.bin (the bitstream as the SPI flash holds it, from build/gbc_top/gbc_top.fs)
and opgbc_fw.bin (fw/build/fw.bin). Copy them to the root of the SD card.
"""
import os
import shutil
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')


def fs_to_bin(fs_path):
    """A Gowin .fs file is the bitstream as text, one '0'/'1' per bit; comment lines start with //."""
    bits = []
    for line in open(fs_path):
        line = line.strip()
        if not line or line.startswith('//'):
            continue
        bits.append(line)
    s = ''.join(bits)
    s += '0' * (-len(s) % 8)
    return bytes(int(s[i:i + 8], 2) for i in range(0, len(s), 8))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'sd_update')
    os.makedirs(out, exist_ok=True)
    fs = os.path.join(ROOT, 'build', 'gbc_top', 'gbc_top.fs')
    fw = os.path.join(ROOT, 'fw', 'build', 'fw.bin')
    if os.path.exists(fs):
        data = fs_to_bin(fs)
        assert len(data) <= 0x100000, 'bitstream larger than the 1 MB before the firmware'
        open(os.path.join(out, 'opgbc_fpga.bin'), 'wb').write(data)
        print('opgbc_fpga.bin  %7d bytes  (%s)' % (len(data), os.path.relpath(fs, ROOT)))
    else:
        print('no bitstream (run ./build.sh gbc_top)')
    if os.path.exists(fw):
        shutil.copyfile(fw, os.path.join(out, 'opgbc_fw.bin'))
        print('opgbc_fw.bin    %7d bytes  (%s)' % (os.path.getsize(fw), os.path.relpath(fw, ROOT)))
    else:
        print('no firmware (run bash fw/build.sh)')
    print('copy them to the root of the SD card, then Menu > System > System Update')


if __name__ == '__main__':
    main()
