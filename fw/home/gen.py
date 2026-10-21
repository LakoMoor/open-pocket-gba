#!/usr/bin/env python3
"""Generates the data of the home screen ROM (home.asm): three scenes with their tiles, maps,
palettes, per-line colours and songs, plus the shared logo, sprites and intro tables.

Output: build/data.inc (everything) and build/sc<N>_bg.2bpp (BG tiles of each scene).

Common screen layout: lines 0..7 sky, 8..55 the logo (map rows 1..6, SCY = logo offset).
  0 Synthwave  sun rows 12..16 (lines 56..95, SCY 40 - sunrise), floor rows 18..23 (96..143, SCY 48)
  1 Space      star field rows 7..17 (lines 56..143, SCX scrolls), planet in the window (88,64)
  2 City       far skyline rows 8..12 (lines 64..103), near skyline rows 13..17 (104..143),
               each with its own SCX; moon and rain are sprites
Map rows 24..31 and row 0 stay blank: the logo drops in from there.
"""
import math
import os
import random

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'build')
os.makedirs(OUT, exist_ok=True)

# ---------------------------------------------------------------- 5x7 font
FONT = {
    'O': ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    'P': ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    'E': ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    'N': ["10001", "11001", "10101", "10011", "10001", "10001", "10001"],
    'C': ["01110", "10001", "10000", "10000", "10000", "10001", "01110"],
    'K': ["10001", "10010", "10100", "11000", "10100", "10010", "10001"],
    'T': ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
    'R': ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    'S': ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    'A': ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    'G': ["01110", "10001", "10000", "10111", "10001", "10001", "01111"],
    'B': ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
}


def canvas(w, h):
    return [[0] * w for _ in range(h)]


def text_mask(s, scale, gap):
    w = len(s) * (5 * scale + gap) - gap
    m = canvas(w, 7 * scale)
    for i, ch in enumerate(s):
        g = FONT[ch]
        for y in range(7 * scale):
            for x in range(5 * scale):
                if g[y // scale][x // scale] == '1':
                    m[y][i * (5 * scale + gap) + x] = 1
    return m


def blit_logo(img, mask, ox, oy):
    """Chrome letters: white top half, cyan bottom half, dark outline and drop shadow (colour 3)."""
    h, w = len(mask), len(mask[0])
    H, W = len(img), len(img[0])
    def at(x, y):
        return 0 <= y < h and 0 <= x < w and mask[y][x]
    for y in range(-2, h + 3):
        for x in range(-2, w + 3):
            X, Y = ox + x, oy + y
            if not (0 <= X < W and 0 <= Y < H):
                continue
            if at(x, y):
                img[Y][X] = 1 if y < h * 0.5 else 2
            elif any(at(x + dx, y + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1)) or at(x - 2, y - 2) or at(x - 1, y - 2) or at(x - 2, y - 1):
                img[Y][X] = 3


logo = canvas(160, 48)
m1 = text_mask("OPEN", 2, 4)
m2 = text_mask("POCKET", 3, 4)
blit_logo(logo, m1, (160 - len(m1[0])) // 2, 2)
blit_logo(logo, m2, (160 - len(m2[0])) // 2, 21)
gb = text_mask("GBC", 1, 1)
bx, by = (160 + len(m2[0])) // 2 - len(gb[0]) - 1, 21 + 21 + 3
for y in range(-1, 8):
    for x in range(-2, len(gb[0]) + 2):
        if 0 <= by + y < 48:
            logo[by + y][bx + x] = 1 if (0 <= y < 7 and 0 <= x < len(gb[0]) and gb[y][x]) else 3


def tile_of(img, tx, ty):
    rows = []
    for y in range(8):
        lo = hi = 0
        for x in range(8):
            c = img[ty * 8 + y][tx * 8 + x]
            lo |= (c & 1) << (7 - x)
            hi |= ((c >> 1) & 1) << (7 - x)
        rows += [lo, hi]
    return bytes(rows)


class Pool:
    """BG tiles of one scene (BG index 0 = blank)."""
    def __init__(self):
        self.tiles = [bytes(16)]
        self.index = {bytes(16): 0}

    def add(self, t):
        if t not in self.index:
            self.index[t] = len(self.tiles)
            self.tiles.append(t)
        return self.index[t]

    def tilemap(self, img):
        w = len(img[0]) // 8
        return [[self.add(tile_of(img, tx, ty)) for tx in range(w)] for ty in range(len(img) // 8)]


def rgb(r, g, b):
    c = lambda v: min(31, max(0, int(round(v))))
    return (c(b) << 10) | (c(g) << 5) | c(r)


def lerp_stops(stops, y):
    for (y0, c0), (y1, c1) in zip(stops, stops[1:]):
        if y0 <= y <= y1:
            t = (y - y0) / (y1 - y0) if y1 > y0 else 0
            return [a + (b - a) * t for a, b in zip(c0, c1)]
    return stops[-1][1]


def col_table(stops, levels=9):
    """levels x 144 colours: fade-in 0..7 (7 = full), 8 = flash (lightning)."""
    out = []
    for lvl in range(levels):
        k = (lvl + 1) / 8.0 if lvl < 8 else 1.0
        for y in range(144):
            c = lerp_stops(stops, y)
            if lvl == 8:
                c = [min(31, v * 1.6 + 9) for v in c]
            out.append(rgb(*[v * k for v in c]))
    return out


NAMES = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def N(s):
    """'A4' -> note byte (MIDI - 34); '-' hold; 'x' off."""
    if s == '-':
        return 0
    if s == 'x':
        return 1
    pc = NAMES[s[0]] + (1 if '#' in s else 0) - (1 if s[1] == 'b' else 0)
    return 12 * (int(s[-1]) + 1) + pc - 34


def seq(text, rows):
    v = [N(t) for t in text.split()]
    assert len(v) == rows, (len(v), rows, text)
    return v


KICK, SNARE, HAT, CRASH, OHAT = 2, 3, 4, 5, 6
out = []


def emit(label, data, per=16, kind='db'):
    out.append('%s::' % label)
    for i in range(0, len(data), per):
        out.append('    %s %s' % (kind, ', '.join(('$%04X' if kind == 'dw' else '$%02X') % (v & (0xFFFF if kind == 'dw' else 0xFF)) for v in data[i:i + per])))


scenes = []

# ================================================================ scene 0: synthwave
def scene_synthwave():
    p = Pool()
    sun = canvas(160, 40)
    R, CX = 38, 80
    for y in range(40):
        dy = 40 - y - 0.5
        gapped = y >= 18 and ((y - 18) % 6) >= 6 - (1 + (y - 18) // 6)
        for x in range(160):
            if (x - CX + 0.5) ** 2 + dy * dy <= R * R and not gapped:
                sun[y][x] = 1 if y < 14 else 2 if y < 27 else 3
    floor = canvas(160, 48)
    for x in range(160):
        floor[0][x] = 3
    for k in range(-9, 10):
        xb = 80 + k * 26
        for y in range(3, 48):
            x = int(round(80 + (xb - 80) * y / 47.0))
            if 0 <= x < 160:
                floor[y][x] = 3
    maps = [(1, p.tilemap(logo)), (12, p.tilemap(sun)), (18, p.tilemap(floor))]
    attrs = [(12, 5, 32, 1)]
    SKY = [(0, (1, 1, 6)), (30, (5, 2, 12)), (60, (13, 3, 16)), (85, (25, 7, 16)), (95, (31, 14, 12)), (96, (4, 1, 8)), (143, (2, 0, 5))]
    dmg = [((3 if y < 60 else 2 if y < 88 else 1) if y < 96 else 3) | 0x90 for y in range(144)]
    bgpal = [0, rgb(31, 31, 31), rgb(10, 26, 31), rgb(31, 6, 26), 0, rgb(31, 30, 10), rgb(31, 18, 6), rgb(31, 6, 16)]
    # song: A minor synthwave
    rows = []
    lead_i = seq("A4 - C5 - E5 - A5 - C6 - E6 - A6 " + "- " * 16 + "x - -", 32)
    arp_i = [0] * 32; arp_i[16] = N('A3')
    bass_i = [0] * 32; bass_i[0] = N('A3')
    drum_i = [0] * 32; drum_i[0] = CRASH
    for r in range(16, 32, 2): drum_i[r] = SNARE
    for r in range(24, 32): drum_i[r] = SNARE
    drum_i[16] = KICK
    rows += [[lead_i[r], arp_i[r], bass_i[r], drum_i[r]] for r in range(32)]
    loop = len(rows)
    melody = ["E5 - - - A5 - - - G5 - E5 - D5 - - -", "C5 - - - - - A4 - C5 - D5 - E5 - - -",
              "G5 - - - E5 - - - C5 - D5 - E5 - - -", "D5 - - - - - - - B4 - - - D5 - - -",
              "A5 - - - C6 - - - B5 - A5 - G5 - - -", "A5 - - - - - F5 - G5 - A5 - C6 - - -",
              "G5 - - - E5 - G5 - C6 - - - B5 - A5 -", "B5 - - - - - - - D6 - - - - - x -"]
    chords = [('A3', 0), ('F3', 1), ('C4', 1), ('G3', 1)]
    bassn = ['A2', 'F2', 'C3', 'G2']
    for lp in range(2):
        for b in range(8):
            root, major = chords[b % 4]
            mel = seq(melody[b], 16) if lp == 1 or b >= 4 else [0] * 16
            arp = [0] * 16
            for r in (0, 4, 8, 12): arp[r] = N(root) | (0x80 if major else 0)
            bass = [0] * 16
            for r in range(0, 16, 2): bass[r] = N(bassn[b % 4]) + 12 + (12 if r in (6, 14) else 0)
            d = [0] * 16
            d[0] = d[8] = KICK; d[4] = d[12] = SNARE
            for r in (2, 6, 10, 14): d[r] = HAT
            if b % 4 == 3: d[14] = OHAT; d[11] = KICK
            if lp == 1 and b == 7: d[13] = d[14] = d[15] = SNARE
            if b == 0 and lp == 0: d[0] = CRASH
            rows += [[mel[r], arp[r], bass[r], d[r]] for r in range(16)]
    return dict(pool=p, maps=maps, attrs=attrs, col=col_table(SKY), dmg=dmg, bgpal=bgpal,
                song=rows, loop=loop, tempo=7, arp7=0, wx=0, wy=0)


# ================================================================ scene 1: space
def scene_space():
    p = Pool()
    rnd = random.Random(11)
    # star field: a few star tile variants spread over 32 x 11 cells (wraps when scrolling)
    variants = []
    for v in range(10):
        img = canvas(8, 8)
        for _ in range(1 if v < 6 else 2):
            img[rnd.randint(0, 7)][rnd.randint(0, 7)] = rnd.choice([1, 2, 2])
        if v == 9:
            img[3][3] = img[3][4] = img[4][3] = img[4][4] = 1
        variants.append(img)
    field = canvas(256, 88)
    for ty in range(11):
        for tx in range(32):
            if rnd.random() < 0.45:
                v = variants[rnd.randrange(len(variants))]
                for y in range(8):
                    for x in range(8):
                        field[ty * 8 + y][tx * 8 + x] = v[y][x]
    # planet with a ring in the window (72 x 80 at 88, 64): colour 1 lit, 2 shade, 3 ring / stars
    win = canvas(72, 80)
    for _ in range(14):
        win[rnd.randint(0, 79)][rnd.randint(0, 71)] = 3
    cx, cy, R = 40, 42, 22
    ring = lambda x, y: abs(((x - cx) / 34.0) ** 2 + ((y - cy) / 9.0) ** 2 - 1.0) < 0.16
    for y in range(80):
        for x in range(72):
            dx, dy = x - cx + 0.5, y - cy + 0.5
            inside = dx * dx + dy * dy <= R * R
            front = ring(x, y) and (y >= cy or not inside)
            if front:
                win[y][x] = 3
            elif inside:
                lit = (dx * -0.6 + dy * -0.8) / R
                band = int((y - cy + R) / 5) % 2
                win[y][x] = 1 if lit > -0.25 + 0.1 * band else 2
            elif ring(x, y):
                win[y][x] = 3
    maps = [(1, p.tilemap(logo)), (7, p.tilemap(field)), ('W0', p.tilemap(win))]
    attrs = [('W0', 10, 9, 1)]
    SKY = [(0, (0, 0, 2)), (30, (2, 1, 7)), (60, (9, 2, 14)), (80, (12, 3, 16)), (100, (5, 2, 11)), (143, (0, 0, 3))]
    dmg = [(3 if y < 56 or y > 110 else 2) | 0x90 for y in range(144)]
    bgpal = [0, rgb(31, 31, 31), rgb(10, 26, 31), rgb(31, 6, 26), 0, rgb(31, 22, 12), rgb(14, 7, 6), rgb(24, 24, 31)]
    # song: slow ambient in A minor, 7th arpeggios
    rows = []
    prog = [('A3', 0), ('F3', 1), ('D3', 0), ('E3', 0)]
    lead = ["E5 - - - - - - - - - - - D5 - C5 -", "A4 - - - - - - - - - - - - - - -",
            "F5 - - - - - - - E5 - - - D5 - - -", "E5 - - - - - - - B4 - - - - - x -"]
    bassn = ['A2', 'F2', 'D2', 'E2']
    for lp in range(4):
        for b in range(4):
            root, major = prog[b]
            arp = [0] * 16
            arp[0] = N(root) | (0x80 if major else 0)
            arp[8] = arp[0]
            mel = seq(lead[b], 16) if lp % 2 == 1 else [0] * 16
            bass = [0] * 16; bass[0] = N(bassn[b]) + 12
            d = [0] * 16
            if lp >= 1:
                d[0] = KICK if lp >= 2 else 0
                d[8] = HAT; d[12] = HAT if lp >= 2 else 0
            if lp == 0 and b == 0:
                d[0] = CRASH
            rows += [[mel[r], arp[r], bass[r], d[r]] for r in range(16)]
    return dict(pool=p, maps=maps, attrs=attrs, col=col_table(SKY), dmg=dmg, bgpal=bgpal,
                song=rows, loop=16, tempo=10, arp7=1, wx=88 + 7, wy=64)


# ================================================================ scene 2: night city
def skyline(w, h, rnd, base_h, var_h, colour, lit_p, win_rows):
    img = canvas(w, h)
    x = 0
    while x < w:
        bw = rnd.choice([16, 16, 24, 24, 32])
        bh = base_h + rnd.randrange(0, var_h + 1, 8)
        top = h - bh
        for y in range(max(0, top), h):
            for xx in range(x, min(w, x + bw)):
                img[y][xx] = colour
        # windows: 2x2 px on a 4 px grid, lit at random (colour 3)
        for y in range(top + 3, h - 2, 4):
            for xx in range(x + 1, min(w, x + bw) - 1, 4):
                if rnd.random() < lit_p and y in range(0, h):
                    for a in range(2):
                        for b in range(2):
                            if 0 <= y + a < h and xx + b < w:
                                img[y + a][xx + b] = 3
        if rnd.random() < 0.3 and top > 2:             # antenna
            ax = x + bw // 2
            for y in range(max(0, top - 6), top):
                img[y][ax] = colour
        x += bw
    return img


def scene_city():
    p = Pool()
    rnd = random.Random(5)
    far = skyline(256, 40, rnd, 8, 32, 1, 0.18, 0)
    near = skyline(256, 40, rnd, 16, 24, 2, 0.35, 0)
    maps = [(1, p.tilemap(logo)), (8, p.tilemap(far)), (13, p.tilemap(near))]
    attrs = [(8, 10, 32, 1)]
    SKY = [(0, (2, 1, 8)), (24, (5, 2, 13)), (50, (12, 4, 16)), (80, (26, 10, 14)), (103, (30, 15, 10)),
           (104, (4, 3, 10)), (143, (2, 1, 6))]
    dmg = [((3 if y < 40 else 2 if y < 80 else 1) | 0x90) if y < 64 else (((2 if y < 104 else 3)) | 0x38 if True else 0) for y in range(144)]
    dmg = [((3 if y < 40 else 2 if y < 80 else 1) | 0x90) if y < 64 else ((1 if y < 104 else 2) | (2 << 2) | (3 << 4)) for y in range(144)]
    bgpal = [0, rgb(31, 31, 31), rgb(10, 26, 31), rgb(31, 6, 26), 0, rgb(7, 8, 17), rgb(1, 1, 3), rgb(31, 25, 12)]
    # song: lo-fi with 7th chords
    rows = []
    prog = [('F3', 1), ('E3', 0), ('D3', 0), ('C3', 1)]
    bassn = ['F2', 'E2', 'D2', 'C2']
    mel = ["A4 - - - C5 - - - E5 - - - D5 - C5 -", "B4 - - - - - - - G4 - - - - - - -",
           "A4 - - - F4 - - - A4 - C5 - D5 - - -", "E5 - - - - - - - - - - - x - - -"]
    for lp in range(2):
        for b in range(4):
            root, major = prog[b]
            arp = [0] * 16
            for r in (0, 8): arp[r] = N(root) | (0x80 if major else 0)
            m = seq(mel[b], 16) if lp == 1 else [0] * 16
            bass = [0] * 16
            bass[0] = N(bassn[b]) + 12; bass[6] = N(bassn[b]) + 19; bass[10] = N(bassn[b]) + 12
            d = [0] * 16
            d[0] = KICK; d[7] = KICK; d[10] = KICK; d[4] = SNARE; d[12] = SNARE
            for r in (2, 6, 10, 14): d[r] = d[r] or HAT
            rows += [[m[r], arp[r], bass[r], d[r]] for r in range(16)]
    return dict(pool=p, maps=maps, attrs=attrs, col=col_table(SKY), dmg=dmg, bgpal=bgpal,
                song=rows, loop=0, tempo=8, arp7=1, wx=0, wy=0)


scenes = [scene_synthwave(), scene_space(), scene_city()]

for i, sc in enumerate(scenes):
    p = sc['pool']
    assert len(p.tiles) <= 256, (i, len(p.tiles))
    open(os.path.join(OUT, 'sc%d_bg.2bpp' % i), 'wb').write(b''.join(p.tiles))
    # map list: dw dest, db rows, db width, dw src
    lst = []
    for j, (row, m) in enumerate(sc['maps']):
        dest = 0x9C00 if row == 'W0' else 0x9800 + 32 * row
        emit('S%d_Map%d' % (i, j), [t for r in m for t in r], 20)
        lst.append((dest, len(m), len(m[0]), 'S%d_Map%d' % (i, j)))
    out.append('S%d_MapList::' % i)
    for dest, rows, w, lab in lst:
        out.append('    dw $%04X\n    db %d, %d\n    dw %s' % (dest, rows, w, lab))
    out.append('    dw 0')
    out.append('S%d_AttrList::' % i)
    for row, rows, w, pal in sc['attrs']:
        dest = 0x9C00 if row == 'W0' else 0x9800 + 32 * row
        out.append('    dw $%04X\n    db %d, %d, %d' % (dest, rows, w, pal))
    out.append('    dw 0')
    emit('S%d_Col' % i, sc['col'], 8, 'dw')
    emit('S%d_Dmg' % i, sc['dmg'])
    emit('S%d_BgPal' % i, sc['bgpal'], 8, 'dw')
    emit('S%d_Song' % i, [v for r in sc['song'] for v in r])
    out.append('S%d_Tiles::\n    INCBIN "build/sc%d_bg.2bpp"\nS%d_TilesEnd::' % (i, i, i))
    print('scene %d: %d BG tiles, %d song rows' % (i, len(p.tiles), len(sc['song'])))

out.append('SceneTable::')
for i, sc in enumerate(scenes):
    out.append('    dw S%d_Tiles, S%d_TilesEnd - S%d_Tiles, S%d_MapList, S%d_AttrList, S%d_BgPal, S%d_Col, S%d_Dmg, S%d_Song' % ((i,) * 9))
    out.append('    dw %d, %d' % (len(sc['song']), sc['loop']))
    out.append('    db %d, %d, %d, %d' % (sc['tempo'], sc['arp7'], sc['wx'], sc['wy']))
out.append('DEF NSCENES EQU %d' % len(scenes))
out.append('DEF SCENE_SIZE EQU 24')

# ---------------------------------------------------------------- shared tables
FLOOR_OFF = [(0, (12, 3, 14)), (10, (6, 1, 10)), (47, (2, 0, 5))]
FLOOR_ON = [(0, (26, 8, 22)), (12, (31, 6, 26)), (47, (31, 10, 31))]
fc = []
for lvl in range(8):
    k = (lvl + 1) / 8.0
    fc += [rgb(*[v * k for v in lerp_stops(FLOOR_OFF, y)]) for y in range(48)]
    fc += [rgb(*[v * k for v in lerp_stops(FLOOR_ON, y)]) for y in range(48)]
emit('FloorCol', fc, 8, 'dw')
emit('FloorZ', [int(1400 / (y + 1.5)) & 0xFF for y in range(49)])

INTRO = 200
fade, logo_d, sun_s, amp = [], [], [], []
def bounce(x):
    n1, d1 = 7.5625, 2.75
    if x < 1 / d1: return n1 * x * x
    if x < 2 / d1: x -= 1.5 / d1; return n1 * x * x + 0.75
    if x < 2.5 / d1: x -= 2.25 / d1; return n1 * x * x + 0.9375
    x -= 2.625 / d1; return n1 * x * x + 0.984375
for f in range(INTRO):
    fade.append(min(7, f // 6))
    t = min(1.0, max(0.0, (f - 20) / 70.0))
    logo_d.append(int(round(-64 * (1 - bounce(t)))) & 0xFF)
    st = min(1.0, f / 110.0)
    sun_s.append(int(round(40 * (1 - st) ** 3)) if f < 110 else 0)
    amp.append(7 if f < 90 else max(0, 7 - (f - 90) // 14))
emit('IntroFade', fade)
emit('IntroLogo', logo_d)
emit('IntroSun', sun_s)
emit('IntroAmp', amp)
out.append('DEF INTRO_LEN EQU %d' % INTRO)
emit('Bob', [int(round(2 * math.sin(2 * math.pi * i / 128))) & 0xFF for i in range(128)])
wob = []
for a in range(8):
    A = [0, 1, 2, 3, 5, 8, 12, 18][a]
    wob += [int(round(A * math.sin(2 * math.pi * q / 64))) & 0xFF for q in range(64)]
emit('Wobble', wob)
c1, c2 = [], []
for i in range(64):
    t = (1 - math.cos(2 * math.pi * i / 64)) / 2
    c1.append(rgb(31, 31 - 4 * t, 31 - 2 * t))
    c2.append(rgb(8 + 18 * t, 26 - 12 * t, 31))
emit('LogoC1', c1, 8, 'dw')
emit('LogoC2', c2, 8, 'dw')
rnd = random.Random(7)
st = []
for i in range(18):
    st += [rnd.randint(8, 166), rnd.randint(18, 100), rnd.randint(0, 31)]
emit('Stars', st, 12)
out.append('DEF NSTARS EQU 18')
rain = []
for i in range(22):
    rain += [rnd.randint(0, 255), rnd.randint(0, 175)]
emit('RainInit', rain, 16)
out.append('DEF NRAIN EQU 22')

def period(midi):
    f = 440.0 * 2 ** ((midi - 69) / 12.0)
    return max(0, min(2047, int(round(2048 - 131072 / f))))
emit('NotePeriods', [period(n + 34) for n in range(0, 100)], 8, 'dw')

# ---------------------------------------------------------------- sprites
def sprite(rows):
    return tile_of([[int(c) if c != '.' else 0 for c in r] for r in rows], 0, 0)
obj = [bytes(16)]
obj += [sprite(s) for s in [
    ["........", "........", "........", "...3....", "........", "........", "........", "........"],
    ["........", "........", "...3....", "..333...", "...3....", "........", "........", "........"],
    ["...1....", "...2....", "..222...", "1233321.", "..222...", "...2....", "...1....", "........"],
    ["........", "...1....", "..131...", ".13331..", "..131...", "...1....", "........", "........"]]]
for ch in "PRESTA":                                  # 5..10
    img = canvas(8, 8)
    g = FONT[ch]
    for y in range(7):
        for x in range(5):
            if g[y][x] == '1':
                img[y][x + 1] = 3
    for y in range(8):
        for x in range(8):
            if img[y][x] == 0 and any(0 <= y + dy < 8 and 0 <= x + dx < 8 and img[y + dy][x + dx] == 3
                                      for dx in (-1, 0, 1) for dy in (-1, 0, 1)):
                img[y][x] = 1
    obj.append(tile_of(img, 0, 0))
moon = canvas(16, 16)                               # 11..14: moon, 2 x 2 tiles
for y in range(16):
    for x in range(16):
        d = (x - 7.5) ** 2 + (y - 7.5) ** 2
        if d <= 56:
            moon[y][x] = 3 if (x - 10) ** 2 + (y - 5) ** 2 > 30 else 2
            if (x - 5) ** 2 + (y - 9) ** 2 < 5 or (x - 9) ** 2 + (y - 12) ** 2 < 3:
                moon[y][x] = 1
obj += [tile_of(moon, 0, 0), tile_of(moon, 1, 0), tile_of(moon, 0, 1), tile_of(moon, 1, 1)]
obj.append(sprite(["......3.", ".....3..", ".....3..", "....3...", "....3...", "...3....", "...3....", "........"]))   # 15 rain
obj.append(sprite(["........", "......33", "....3333", "...33332", "....3333", "......33", "........", "........"]))   # 16 meteor head
obj.append(sprite(["........", "........", "...1122.", "1112222.", "...1122.", "........", "........", "........"]))   # 17 meteor tail
open(os.path.join(OUT, 'gfx_obj.2bpp'), 'wb').write(b''.join(obj))

open(os.path.join(OUT, 'data.inc'), 'w').write('\n'.join(out) + '\n')
print('obj tiles %d' % len(obj))
