#!/usr/bin/env python3
"""Generates the data of the home screen ROM (home.asm): tiles, maps, raster tables, music.

Output: build/gfx_bg.2bpp (BG tiles by BG index), build/gfx_obj.2bpp (sprite tiles),
build/data.inc (maps, attributes, tables, song).

Screen layout (scanlines):   0..7 sky | 8..55 logo | 56..95 sun | 96..143 floor
BG map rows:  1..6 logo, 12..16 sun, 18..23 floor (shown with SCY = 48), the rest blank.
"""
import math
import os

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


# ---------------------------------------------------------------- BG pictures
logo = canvas(160, 48)
m1 = text_mask("OPEN", 2, 4)
m2 = text_mask("POCKET", 3, 4)
blit_logo(logo, m1, (160 - len(m1[0])) // 2, 2)
blit_logo(logo, m2, (160 - len(m2[0])) // 2, 21)
# a small "GBC" badge under the right end of POCKET, colour 1 on 3
gb = text_mask("GBC", 1, 1)
bx, by = (160 + len(m2[0])) // 2 - len(gb[0]) - 1, 21 + 21 + 3
for y in range(-1, 8):
    for x in range(-2, len(gb[0]) + 2):
        if 0 <= by + y < 48:
            logo[by + y][bx + x] = 1 if (0 <= y < 7 and 0 <= x < len(gb[0]) and gb[y][x]) else 3

sun = canvas(160, 40)                       # bottom edge = horizon
R, CX = 38, 80
for y in range(40):
    dy = 40 - y - 0.5
    gapped = y >= 18 and ((y - 18) % 6) >= 6 - (1 + (y - 18) // 6)   # stripes thicken downwards
    for x in range(160):
        if (x - CX + 0.5) ** 2 + dy * dy <= R * R and not gapped:
            sun[y][x] = 1 if y < 14 else 2 if y < 27 else 3

floor = canvas(160, 48)
for x in range(160):
    floor[0][x] = 3                          # horizon
for k in range(-9, 10):
    xb = 80 + k * 26                         # where the line meets the bottom edge
    for y in range(3, 48):
        t = y / 47.0
        x = int(round(80 + (xb - 80) * t))
        if 0 <= x < 160:
            floor[y][x] = 3

# ---------------------------------------------------------------- tiles
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


bg_tiles = [bytes(16)]                       # 0: blank
bg_index = {bytes(16): 0}


def tilemap(img):
    m = []
    for ty in range(len(img) // 8):
        row = []
        for tx in range(20):
            t = tile_of(img, tx, ty)
            if t not in bg_index:
                bg_index[t] = len(bg_tiles)
                bg_tiles.append(t)
            row.append(bg_index[t])
        m.append(row)
    return m


logo_map = tilemap(logo)
sun_map = tilemap(sun)
floor_map = tilemap(floor)
assert len(bg_tiles) <= 256, len(bg_tiles)
open(os.path.join(OUT, 'gfx_bg.2bpp'), 'wb').write(b''.join(bg_tiles) + bytes(16 * (256 - len(bg_tiles))))

# sprites: 0 blank, 1..4 star twinkle frames, 5.. letters with an outline
obj = [bytes(16)]
stars = [
    ["........", "........", "........", "...3....", "........", "........", "........", "........"],
    ["........", "........", "...3....", "..333...", "...3....", "........", "........", "........"],
    ["...1....", "...2....", "..222...", "1233321.", "..222...", "...2....", "...1....", "........"],
    ["........", "...1....", "..131...", ".13331..", "..131...", "...1....", "........", "........"],
]
for s in stars:
    img = [[int(c) if c != '.' else 0 for c in row] for row in s]
    obj.append(tile_of(img, 0, 0))
LETTERS = "PRESTA"
for ch in LETTERS:
    img = canvas(8, 8)
    g = FONT[ch]
    for y in range(7):
        for x in range(5):
            if g[y][x] == '1':
                img[y][x + 1] = 3
    for y in range(8):                       # outline in colour 1
        for x in range(8):
            if img[y][x] == 0 and any(0 <= y + dy < 8 and 0 <= x + dx < 8 and img[y + dy][x + dx] == 3
                                      for dx in (-1, 0, 1) for dy in (-1, 0, 1)):
                img[y][x] = 1
    obj.append(tile_of(img, 0, 0))
open(os.path.join(OUT, 'gfx_obj.2bpp'), 'wb').write(b''.join(obj))

# ---------------------------------------------------------------- tables
def rgb(r, g, b):
    return (min(31, max(0, int(round(b)))) << 10) | (min(31, max(0, int(round(g)))) << 5) | min(31, max(0, int(round(r))))


def lerp_stops(stops, y):
    for (y0, c0), (y1, c1) in zip(stops, stops[1:]):
        if y0 <= y <= y1:
            t = (y - y0) / (y1 - y0) if y1 > y0 else 0
            return [a + (b - a) * t for a, b in zip(c0, c1)]
    return stops[-1][1]


SKY = [(0, (1, 1, 6)), (30, (5, 2, 12)), (60, (13, 3, 16)), (85, (25, 7, 16)), (95, (31, 14, 12))]
FLOOR_OFF = [(0, (12, 3, 14)), (10, (6, 1, 10)), (47, (2, 0, 5))]
FLOOR_ON = [(0, (26, 8, 22)), (12, (31, 6, 26)), (47, (31, 10, 31))]

out = []
def emit(label, data, per=16, kind='db'):
    out.append('%s::' % label)
    for i in range(0, len(data), per):
        out.append('    %s %s' % (kind, ', '.join(('$%04X' if kind == 'dw' else '$%02X') % v for v in data[i:i + per])))


# fade levels 0..7 (7 = full). SkyCol: [level][96 lines]; FloorCol: [level][off 48, on 48]
sky_col, floor_col = [], []
for lvl in range(8):
    k = (lvl + 1) / 8.0
    sky_col += [rgb(*[v * k for v in lerp_stops(SKY, y)]) for y in range(96)]
    floor_col += [rgb(*[v * k for v in lerp_stops(FLOOR_OFF, y)]) for y in range(48)]
    floor_col += [rgb(*[v * k for v in lerp_stops(FLOOR_ON, y)]) for y in range(48)]
emit('SkyCol', sky_col, 8, 'dw')
emit('FloorCol', floor_col, 8, 'dw')

# DMG: BGP per line. Colour 1..3 -> shades 0, 1, 2; colour 0 is the line shade.
dmg_sky = [(3 if y < 60 else 2 if y < 88 else 1) | 0x90 for y in range(96)]
emit('DmgSky', dmg_sky)

# floor depth: z = K / (y + 1), 8 bit wrapping; a grid line wherever z crosses a multiple of 32
K = 1400
emit('FloorZ', [int(K / (y + 1.5)) & 0xFF for y in range(49)])

# intro: per frame fade level, logo offset (SCY, signed), sun offset, wobble amplitude index
INTRO = 200
fade, logo_d, sun_s, amp = [], [], [], []
for f in range(INTRO):
    fade.append(min(7, f // 6))
    t = min(1.0, max(0.0, (f - 20) / 70.0))
    # drop from above with a bounce (easeOutBounce)
    def bounce(x):
        n1, d1 = 7.5625, 2.75
        if x < 1 / d1:
            return n1 * x * x
        if x < 2 / d1:
            x -= 1.5 / d1; return n1 * x * x + 0.75
        if x < 2.5 / d1:
            x -= 2.25 / d1; return n1 * x * x + 0.9375
        x -= 2.625 / d1; return n1 * x * x + 0.984375
    d = int(round(-64 * (1 - bounce(t))))
    logo_d.append(d & 0xFF)
    st = min(1.0, f / 110.0)
    sun_s.append(int(round(40 * (1 - (1 - (1 - st) ** 3)))) if f < 110 else 0)
    amp.append(7 if f < 90 else max(0, 7 - (f - 90) // 14))
emit('IntroFade', fade)
emit('IntroLogo', logo_d)
emit('IntroSun', sun_s)
emit('IntroAmp', amp)
out.append('DEF INTRO_LEN EQU %d' % INTRO)

# idle logo bob, 128 frames, +-2
emit('Bob', [int(round(2 * math.sin(2 * math.pi * i / 128))) & 0xFF for i in range(128)])
# wobble: 8 amplitudes x 64 phases, signed SCX offsets
wob = []
for a in range(8):
    A = [0, 1, 2, 3, 5, 8, 12, 18][a]
    wob += [int(round(A * math.sin(2 * math.pi * p / 64))) & 0xFF for p in range(64)]
emit('Wobble', wob)
# logo colour cycle (colour 1 and 2 of BG palette 0), 64 steps
c1, c2 = [], []
for i in range(64):
    t = (1 - math.cos(2 * math.pi * i / 64)) / 2
    c1.append(rgb(31, 31 - 4 * t, 31 - 2 * t))
    c2.append(rgb(8 + 18 * t, 26 - 12 * t, 31))
emit('LogoC1', c1, 8, 'dw')
emit('LogoC2', c2, 8, 'dw')

# stars: x, y, phase
import random
random.seed(7)
st = []
for i in range(18):
    st += [random.randint(8, 166), random.randint(18, 100), random.randint(0, 31)]
emit('Stars', st, 12)
out.append('DEF NSTARS EQU 18')

# maps
def emit_map(label, m):
    emit(label, [t for row in m for t in row], 20)
emit_map('LogoMap', logo_map)
emit_map('SunMap', sun_map)
emit_map('FloorMap', floor_map)
out.append('DEF BG_TILES EQU %d' % len(bg_tiles))

# ---------------------------------------------------------------- music
# note index n >= 2 is MIDI note n + 34 (2 = C2). Periods for the pulse channels.
def period(midi):
    f = 440.0 * 2 ** ((midi - 69) / 12.0)
    return max(0, min(2047, int(round(2048 - 131072 / f))))
emit('NotePeriods', [period(n + 34) for n in range(0, 100)], 8, 'dw')

NAMES = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
def N(s):
    """'A4' -> note byte; '-' -> 0 (hold); 'x' -> 1 (off)."""
    if s == '-':
        return 0
    if s == 'x':
        return 1
    pc = NAMES[s[0]] + (1 if '#' in s else 0)
    midi = 12 * (int(s[-1]) + 1) + pc
    return midi - 34

def seq(text, rows):
    v = [N(t) for t in text.split()]
    assert len(v) == rows, (len(v), rows, text)
    return v

KICK, SNARE, HAT, CRASH, OHAT = 2, 3, 4, 5, 6

def bar_drums(variant=0):
    d = [0] * 16
    for r in (0, 8):
        d[r] = KICK
    for r in (4, 12):
        d[r] = SNARE
    for r in (2, 6, 10, 14):
        d[r] = HAT
    if variant == 1:
        d[14] = OHAT
        d[11] = KICK
    if variant == 2:
        d[13], d[14], d[15] = SNARE, SNARE, SNARE
    return d

CHORDS = [('A3', 0), ('F3', 1), ('C4', 1), ('G3', 1)]     # root, major flag
BASS = ['A2', 'F2', 'C3', 'G2']

rows = []
# intro: 2 bars. Rising arpeggio on the lead, bass hold, snare build-up at the end.
lead_i = seq("A4 - C5 - E5 - A5 - C6 - E6 - A6 " + "- " * 16 + "x - -", 32)
arp_i = [0] * 32
arp_i[16] = N('A3')
bass_i = [0] * 32
bass_i[0] = N('A3')                 # wave channel sounds an octave lower: A2
drum_i = [0] * 32
drum_i[0] = CRASH
for r in range(16, 32, 2):
    drum_i[r] = SNARE
for r in range(24, 32):
    drum_i[r] = SNARE
drum_i[16] = KICK
for r in range(32):
    rows.append([lead_i[r], arp_i[r], bass_i[r], drum_i[r]])
loop_start = len(rows)

melody = [
    "E5 - - - A5 - - - G5 - E5 - D5 - - -",
    "C5 - - - - - A4 - C5 - D5 - E5 - - -",
    "G5 - - - E5 - - - C5 - D5 - E5 - - -",
    "D5 - - - - - - - B4 - - - D5 - - -",
    "A5 - - - C6 - - - B5 - A5 - G5 - - -",
    "A5 - - - - - F5 - G5 - A5 - C6 - - -",
    "G5 - - - E5 - G5 - C6 - - - B5 - A5 -",
    "B5 - - - - - - - D6 - - - - - x -",
]
for loop in range(2):
    for b in range(8):
        root, major = CHORDS[b % 4]
        mel = seq(melody[b], 16) if loop == 1 or b >= 4 else [0] * 16
        if loop == 0 and b == 4:
            pass
        arp = [0] * 16
        for r in (0, 4, 8, 12):
            arp[r] = N(root) | (0x80 if major else 0)
        bass = [0] * 16
        bn = BASS[b % 4]
        lo = N(bn) + 12                    # wave channel: one octave below the pulse period
        for r in range(0, 16, 2):
            bass[r] = lo + (12 if r in (6, 14) else 0)
        drums = bar_drums(2 if (loop == 1 and b == 7) else 1 if b % 4 == 3 else 0)
        if b == 0 and loop == 0:
            drums[0] = CRASH
        for r in range(16):
            rows.append([mel[r], arp[r], bass[r], drums[r]])

flat = [v for r in rows for v in r]
emit('Song', flat, 16)
out.append('DEF SONG_ROWS EQU %d' % len(rows))
out.append('DEF SONG_LOOP EQU %d' % loop_start)

open(os.path.join(OUT, 'data.inc'), 'w').write('\n'.join(out) + '\n')
print('bg tiles %d, obj tiles %d, song rows %d' % (len(bg_tiles), len(obj), len(rows)))
