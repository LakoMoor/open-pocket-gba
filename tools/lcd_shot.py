#!/usr/bin/env python3
"""Screenshot of the SPI LCD menu over the USB-UART.

Select in the menu makes the firmware print the 16 x 12 LCD and the 42 x 24 HDMI text
buffers on the log
(fw/src/osd.c lcd_dump); this script presses Select, reads the dump and renders it the
way rtl/video/lcd_st7735.sv does (8 x 10 px cells, rtl/video/font8x8.hex, the OSD palette).

  python tools/lcd_shot.py out.png                     the menu as it is
  python tools/lcd_shot.py out.png --keys "M . D D"     run remote.py-style taps first
  python tools/lcd_shot.py out.png --osd hdmi.png       also the HDMI menu (no capture card needed)
  python tools/lcd_shot.py out.png --dump saved.txt    render a saved dump, no board needed
"""
import argparse
import os
import struct
import sys
import time
import zlib

PAL = {1: 0x000000, 2: 0x0F380F, 3: 0x242424, 4: 0x3C3C3C, 5: 0x7A7A7A, 6: 0xBEBEBE, 7: 0xFFFFFF,
       8: 0x3A6FF0, 9: 0x9BBC0F, 10: 0xF5C518, 11: 0xE5483C, 12: 0x306230, 13: 0x0E1A33, 14: 0x8BAC0F,
       0: 0x101010, 15: 0x000000}
ROOT = os.path.join(os.path.dirname(__file__), '..')


def font():
    rows = [int(l, 16) for l in open(os.path.join(ROOT, 'rtl', 'video', 'font8x8.hex')) if l.strip()]
    return rows


def render(cells, scale, osd=False):
    f = font()
    cw, ch_ = (42, 24) if osd else (16, 12)
    w, h = cw * 8, (240 if osd else 128)
    top = 0 if osd else 4
    img = [[0x000000] * w for _ in range(h)]
    for cy in range(ch_):
        for cx in range(cw):
            v = cells[cy][cx]
            attr, ch = v >> 8, v & 0xFF
            fg, bg = PAL[attr & 15], PAL[attr >> 4]
            ext, code = ch & 0x80, ch & 0x7F
            for py in range(10):
                fy = py - 1
                if fy < 0 or fy > 7:
                    bits = f[code * 8 + (0 if fy < 0 else 7)] if ext else 0
                else:
                    bits = f[code * 8 + fy]
                for px in range(8):
                    on = (bits >> (7 - px)) & 1
                    img[top + cy * 10 + py][cx * 8 + px] = fg if on else bg
    raw = b''
    for y in range(h * scale):
        row = img[y // scale]
        raw += b'\0' + b''.join(struct.pack('>I', row[x // scale])[1:] for x in range(w * scale))
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w * scale, h * scale, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def parse(lines, cols=16, rows=12):
    cells = []
    for l in lines:
        l = l.strip()
        if len(l) == cols * 4:
            cells.append([int(l[i:i + 4], 16) for i in range(0, cols * 4, 4)])
    if len(cells) != rows:
        raise SystemExit('bad dump: %d rows' % len(cells))
    return cells


def tap(port, k, hold=0.12, gap=0.35):
    port.write(k.encode()); time.sleep(hold)
    port.write(k.lower().encode()); time.sleep(gap)


def capture(port_name, keys):
    import serial
    import serial.tools.list_ports
    if not port_name:
        for p in serial.tools.list_ports.comports():
            if p.vid == 0x0403 and p.pid == 0x6010:
                port_name = p.device
    if not port_name:
        raise SystemExit('no Tang Nano 20K UART found')
    with serial.Serial(port_name, 115200, timeout=0.2, write_timeout=1) as s:
        for t in (keys or '').split():
            if t == '.':
                time.sleep(0.35)
            elif t.upper() in 'UDLRABSTMX' and len(t) == 1:
                tap(s, t.upper())
            else:
                time.sleep(float(t))
        time.sleep(0.3)
        s.reset_input_buffer()
        tap(s, 'S', gap=0.1)
        buf, end = b'', time.time() + 3
        while time.time() < end and b'OSD-END' not in buf:
            buf += s.read(4096)
    text = buf.decode('ascii', 'replace')
    if 'LCD-BEGIN' not in text or 'LCD-END' not in text:
        raise SystemExit('no LCD dump received (is the menu open?)')
    lcd = text.split('LCD-BEGIN', 1)[1].split('LCD-END', 1)[0].splitlines()
    osd = text.split('OSD-BEGIN', 1)[1].split('OSD-END', 1)[0].splitlines() if 'OSD-END' in text else []
    return lcd, osd


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--keys')
    ap.add_argument('--port')
    ap.add_argument('--dump')
    ap.add_argument('--osd', help='also write the HDMI menu (42 x 24 cells) to this PNG')
    ap.add_argument('--scale', type=int, default=3)
    a = ap.parse_args()
    if a.dump:
        lcd, osd = open(a.dump).read().splitlines(), []
    else:
        lcd, osd = capture(a.port, a.keys)
    open(a.out, 'wb').write(render(parse(lcd), a.scale))
    print('wrote', a.out)
    if a.osd and osd:
        open(a.osd, 'wb').write(render(parse(osd, 42, 24), 2, osd=True))
        print('wrote', a.osd)


if __name__ == '__main__':
    sys.exit(main())
