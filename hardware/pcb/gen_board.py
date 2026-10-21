"""Open Pocket GBC carrier board: generates open_pocket.kicad_pcb with KiCad's pcbnew.

  E:/tools/kicad/bin/python.exe hardware/pcb/gen_board.py

The netlist lives here (no schematic yet): every part with its footprint, side, position and
pad-to-net map. Coordinates are mm from the lower left corner of the board, y up, the same as
hardware/case/open_pocket_case.scad. Front (F) faces the player: buttons, the screen socket.
Back (B): the Tang Nano 20K socket, power, audio, link port, the battery.

Signals (Tang Nano 20K header pin = FPGA pin): buttons 25 26 27 28 29 30 31 77 and Menu 54;
LCD SCK 73, MOSI 74, RES 71, DC 72, CS 53, BL 52; audio 76 (sigma-delta) and 75 (amp enable);
link SC 41, SI 42, SO 56; W25Q "cartridge" CS 51, CLK 48, DI 55, DO 49.
"""
import csv
import os
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
LIB = os.environ.get('KICAD_FP', 'E:/tools/kicad/share/kicad/footprints') + '/'
W, H = 86.0, 136.0
OX, OY = 100.0, 100.0                       # board placed at (100, 100) in the KiCad page
R_CORNER, R_BIG = 6.0, 22.0                 # case corners; the big one bottom right


def mm(v):
    return pcbnew.FromMM(v)


def P(x, y):
    """board coordinates (y up) -> KiCad (y down)"""
    return pcbnew.VECTOR2I(mm(OX + x), mm(OY + H - y))


board = pcbnew.BOARD()
nets = {}


def net(name):
    if name not in nets:
        n = pcbnew.NETINFO_ITEM(board, name)
        board.Add(n)
        nets[name] = n
    return nets[name]


bom = []


def part(ref, lib, name, x, y, rot=0, back=False, pads=None, value='', mpn=''):
    if lib == 'custom':
        f = name()
    else:
        f = pcbnew.FootprintLoad(LIB + lib + '.pretty', name)
        if f is None:
            raise SystemExit('footprint not found: %s:%s' % (lib, name))
    f.SetReference(ref)
    f.SetValue(value or (name if isinstance(name, str) else ref))
    board.Add(f)
    f.SetPosition(P(x, y))
    f.SetOrientationDegrees(rot)
    if back:
        f.Flip(f.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
    for pad in f.Pads():
        n = (pads or {}).get(pad.GetNumber())
        if n:
            pad.SetNet(net(n))
    bom.append((ref, value or '', (lib + ':' + name) if isinstance(name, str) else lib + ':' + ref, mpn, 'back' if back else 'front'))
    return f


# ---------------------------------------------------------------- custom footprints
def tang_socket():
    """Two 1x20 sockets, rows 20.32 mm apart, numbered like the module (pin 1 at the USB end)."""
    f = pcbnew.FOOTPRINT(board)
    f.SetFPID(pcbnew.LIB_ID('open_pocket', 'TangNano20K_Socket'))
    for i in range(40):
        p = pcbnew.PAD(f)
        num = i + 1
        if num <= 20:
            x, y = -24.13 + 2.54 * i, 10.16
        else:
            x, y = 24.13 - 2.54 * (num - 21), -10.16
        p.SetNumber(str(num))
        p.SetShape(pcbnew.PAD_SHAPE_RECT if num == 1 else pcbnew.PAD_SHAPE_OVAL)
        p.SetAttribute(pcbnew.PAD_ATTRIB_PTH)
        p.SetLayerSet(pcbnew.PAD.PTHMask())
        p.SetSize(pcbnew.VECTOR2I(mm(1.7), mm(1.7)))
        p.SetDrillSize(pcbnew.VECTOR2I(mm(1.0), mm(1.0)))
        p.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
        f.Add(p)
    # outline of the module and its connectors on the silkscreen / courtyard
    for (x1, y1, x2, y2) in [(-27.0, -11.3, 27.0, -11.3), (27.0, -11.3, 27.0, 11.3), (27.0, 11.3, -27.0, 11.3), (-27.0, 11.3, -27.0, -11.3)]:
        s = pcbnew.PCB_SHAPE(f)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(pcbnew.VECTOR2I(mm(x1), mm(y1)))
        s.SetEnd(pcbnew.VECTOR2I(mm(x2), mm(y2)))
        s.SetLayer(pcbnew.F_SilkS)
        s.SetWidth(mm(0.15))
        f.Add(s)
    for txt, x in (('USB-C', -24.0), ('HDMI', 24.0)):
        t = pcbnew.PCB_TEXT(f)
        t.SetText(txt)
        t.SetPosition(pcbnew.VECTOR2I(mm(x), 0))
        t.SetLayer(pcbnew.F_SilkS)
        t.SetTextSize(pcbnew.VECTOR2I(mm(1.2), mm(1.2)))
        f.Add(t)
    return f


def link_port():
    """Game Boy link port socket (Pocket / Color style): 6 contacts on a 2.0 mm pitch in two
    staggered rows plus two mounting pegs. Check the drawing of the part you buy before ordering."""
    f = pcbnew.FOOTPRINT(board)
    f.SetFPID(pcbnew.LIB_ID('open_pocket', 'GB_Link_Port'))
    pins = {'1': (-3.0, 0.0), '3': (0.0, 0.0), '5': (3.0, 0.0), '2': (-3.0, 2.5), '4': (0.0, 2.5), '6': (3.0, 2.5)}
    for num, (x, y) in pins.items():
        p = pcbnew.PAD(f)
        p.SetNumber(num)
        p.SetShape(pcbnew.PAD_SHAPE_OVAL)
        p.SetAttribute(pcbnew.PAD_ATTRIB_PTH)
        p.SetLayerSet(pcbnew.PAD.PTHMask())
        p.SetSize(pcbnew.VECTOR2I(mm(1.5), mm(1.5)))
        p.SetDrillSize(pcbnew.VECTOR2I(mm(0.9), mm(0.9)))
        p.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
        f.Add(p)
    for x in (-6.5, 6.5):
        p = pcbnew.PAD(f)
        p.SetNumber('')
        p.SetShape(pcbnew.PAD_SHAPE_CIRCLE)
        p.SetAttribute(pcbnew.PAD_ATTRIB_NPTH)
        p.SetLayerSet(pcbnew.PAD.UnplatedHoleMask())
        p.SetSize(pcbnew.VECTOR2I(mm(1.6), mm(1.6)))
        p.SetDrillSize(pcbnew.VECTOR2I(mm(1.6), mm(1.6)))
        p.SetPosition(pcbnew.VECTOR2I(mm(x), mm(1.2)))
        f.Add(p)
    return f


# ---------------------------------------------------------------- board outline
def edge_line(a, b):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(P(*a))
    s.SetEnd(P(*b))
    s.SetLayer(pcbnew.Edge_Cuts)
    s.SetWidth(mm(0.1))
    board.Add(s)


def edge_arc(c, start, mid, end):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_ARC)
    s.SetArcGeometry(P(*start), P(*mid), P(*end))
    s.SetLayer(pcbnew.Edge_Cuts)
    s.SetWidth(mm(0.1))
    board.Add(s)


def corner(cx, cy, r, a0):
    import math
    pts = [(cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a))) for a in (a0, a0 + 45, a0 + 90)]
    edge_arc((cx, cy), *pts)


r, R = R_CORNER, R_BIG
edge_line((r, 0), (W - R, 0))
corner(W - R, R, R, 270)
edge_line((W, R), (W, H - r))
corner(W - r, H - r, r, 0)
edge_line((W - r, H), (r, H))
corner(r, H - r, r, 90)
edge_line((0, H - r), (0, r))
corner(r, r, r, 180)

# ---------------------------------------------------------------- parts
SMD_SW = ('Button_Switch_SMD', 'SW_SPST_PTS645Sx43SMTR92')
SMALL_SW = ('Button_Switch_SMD', 'SW_SPST_TL3342')
R0603 = ('Resistor_SMD', 'R_0603_1608Metric')
C0603 = ('Capacitor_SMD', 'C_0603_1608Metric')
C0805 = ('Capacitor_SMD', 'C_0805_2012Metric')
SOT23 = ('Package_TO_SOT_SMD', 'SOT-23')

# Tang Nano 20K: horizontal behind the screen, HDMI at the left edge (back side mirrors it);
# the parts under it on the back fit below the module on its sockets
TANG = {1: 'LCD_SCK', 2: 'LCD_MOSI', 3: 'AMP_EN', 5: 'KEY_START', 8: 'KEY_LEFT', 9: 'KEY_RIGHT',
        10: 'KEY_UP', 11: 'KEY_DOWN', 12: 'KEY_A', 13: 'KEY_B', 14: 'KEY_SELECT',
        19: '+3V3', 20: 'GND', 21: 'LCD_BL', 22: 'LCD_CS', 23: 'LCD_RES', 24: 'LCD_DC', 25: '+3V3', 26: 'GND',
        29: 'CART_DO', 30: 'CART_DI', 31: 'CART_CLK', 32: 'CART_CS', 33: 'KEY_MENU', 34: 'LINK_SO_3V',
        35: 'LINK_SC_3V', 36: 'LINK_SI_3V', 38: 'AUDIO_PDM', 39: 'GND', 40: 'TANG_5V'}
part('U1', 'custom', tang_socket, 27.6, 104, 0, back=True, pads={str(k): v for k, v in TANG.items()},
     value='Tang Nano 20K', mpn='2x PinSocket 1x20 2.54 mm, low profile')

# buttons (front)
for ref, (x, y), n in [('SW1', (22, 57), 'KEY_UP'), ('SW2', (22, 35), 'KEY_DOWN'), ('SW3', (11, 46), 'KEY_LEFT'),
                       ('SW4', (33, 46), 'KEY_RIGHT'), ('SW5', (68, 52), 'KEY_A'), ('SW6', (56, 42), 'KEY_B')]:
    part(ref, *SMD_SW, x, y, 0, pads={'1': n, '2': 'GND'}, value=n[4:].title(), mpn='C&K PTS645SK43SMTR92')
for ref, (x, y), n in [('SW7', (24, 20), 'KEY_SELECT'), ('SW8', (37, 20), 'KEY_START'), ('SW9', (50, 20), 'KEY_MENU')]:
    part(ref, *SMALL_SW, x, y, 0, pads={'1': n, '2': 'GND'}, value=n[4:].title(), mpn='TE TL3342')

# screens (front): 2.4" ILI9341 module (14 pins at its right end: the panel is turned 180 degrees,
# the driver mirrors it) or 1.44" ST7735 (8 pins)
part('J3', 'Connector_PinSocket_2.54mm', 'PinSocket_1x14_P2.54mm_Vertical', 79.5, 120.5, 0,
     pads={'1': 'SYS_5V', '2': 'GND', '3': 'LCD_CS', '4': 'LCD_RES', '5': 'LCD_DC', '6': 'LCD_MOSI', '7': 'LCD_SCK', '8': 'BL_SW'},
     value='LCD 2.4in ILI9341', mpn='2.4" SPI TFT 240x320 module')
part('J4', 'Connector_PinSocket_2.54mm', 'PinSocket_1x08_P2.54mm_Vertical', 34.1, 127.5, -90,
     pads={'1': '+3V3', '2': 'GND', '3': 'LCD_SCK', '4': 'LCD_MOSI', '5': 'LCD_RES', '6': 'LCD_DC', '7': 'LCD_CS', '8': 'BL_SW'},
     value='LCD 1.44in ST7735 (alt.)', mpn='1.44" SPI TFT 128x128 module')

# backlight switch: BSS138 inverts the BL pin, AO3401 switches 3.3 V to the module's LED pin
part('Q4', *SOT23, 14, 110, 0, back=True, pads={'1': 'LCD_BL', '2': 'GND', '3': 'BL_GATE'}, value='BSS138')
part('Q5', *SOT23, 14, 116, 0, back=True, pads={'1': 'BL_GATE', '2': '+3V3', '3': 'BL_SW'}, value='AO3401')
part('R20', *R0603, 18, 110, 0, back=True, pads={'1': 'BL_GATE', '2': '+3V3'}, value='10k')
part('R21', *R0603, 18, 106, 0, back=True, pads={'1': 'LCD_BL', '2': 'GND'}, value='100k')

# power (back, bottom strip): USB-C charge input, IP5306, battery, switch, Schottky to the Tang
part('J5', 'Connector_USB', 'USB_C_Receptacle_GCT_USB4105-xx-A_16P_TopMnt_Horizontal', 40, 3.5, 180, back=True,
     pads={'A1': 'GND', 'B12': 'GND', 'A12': 'GND', 'B1': 'GND', 'A4': 'VBUS', 'B9': 'VBUS', 'A9': 'VBUS', 'B4': 'VBUS',
           'A5': 'CC1', 'B5': 'CC2', 'SH': 'GND'}, value='USB-C (charge)', mpn='GCT USB4105-GF-A')
part('R1', *R0603, 34, 9, 90, back=True, pads={'1': 'CC1', '2': 'GND'}, value='5.1k')
part('R2', *R0603, 46, 9, 90, back=True, pads={'1': 'CC2', '2': 'GND'}, value='5.1k')
part('U2', 'Package_SO', 'SOIC-8-1EP_3.9x4.9mm_P1.27mm_EP2.29x3mm', 56, 14, 0, back=True,
     pads={'1': 'VBUS', '2': 'IP_LED1', '5': 'IP_KEY', '6': 'VBAT', '7': 'IP_SW', '8': 'IP_VOUT', '9': 'GND'},
     value='IP5306', mpn='Injoinic IP5306')
part('L1', 'Inductor_SMD', 'L_Bourns_SRN6045TA', 66, 14, 0, back=True, pads={'1': 'IP_SW', '2': 'VBAT'}, value='1uH 4A')
part('C1', *C0805, 50, 18, 0, back=True, pads={'1': 'VBUS', '2': 'GND'}, value='10uF')
part('C2', *C0805, 62, 21, 0, back=True, pads={'1': 'VBAT', '2': 'GND'}, value='10uF')
part('C3', *C0805, 66, 21, 0, back=True, pads={'1': 'VBAT', '2': 'GND'}, value='10uF')
part('C4', *C0805, 52, 22, 0, back=True, pads={'1': 'IP_VOUT', '2': 'GND'}, value='22uF')
part('C5', *C0805, 56, 22, 0, back=True, pads={'1': 'IP_VOUT', '2': 'GND'}, value='22uF')
part('R3', *R0603, 50, 8, 0, back=True, pads={'1': 'IP_LED1', '2': 'LED_CHG'}, value='1k')
part('D2', 'LED_SMD', 'LED_0603_1608Metric', 46, 4, 0, back=True, pads={'1': 'GND', '2': 'LED_CHG'}, value='LED charge')
part('J6', 'Connector_JST', 'JST_PH_S2B-PH-K_1x02_P2.00mm_Horizontal', 74, 26, 180, back=True,
     pads={'1': 'VBAT', '2': 'GND'}, value='LiPo 3.7 V', mpn='603450 1200 mAh with PH2.0 lead')
part('SW10', 'Button_Switch_SMD', 'SW_SPDT_Shouhan_MSK12C02', 83, 64, 90, back=True,
     pads={'1': 'IP_VOUT', '2': 'SYS_5V', 'SH': 'GND'}, value='Power', mpn='Shouhan MSK12C02')
part('SW11', *SMALL_SW, 80, 76, 90, back=True, pads={'1': 'IP_KEY', '2': 'GND'}, value='Wake / boost', mpn='TE TL3342')
part('D1', 'Diode_SMD', 'D_SMA', 40, 70, 0, back=True, pads={'1': 'TANG_5V', '2': 'SYS_5V'}, value='SS34')

# audio (back): RC filter of the sigma-delta pin, PAM8302A, speaker and headphone jack
part('R10', *R0603, 24, 16, 0, back=True, pads={'1': 'AUDIO_PDM', '2': 'AUDIO'}, value='1k')
part('C10', *C0603, 24, 13, 0, back=True, pads={'1': 'AUDIO', '2': 'GND'}, value='10nF')
part('C11', *C0603, 28, 18, 90, back=True, pads={'1': 'AUDIO', '2': 'AMP_INP'}, value='1uF')
part('C12', *C0603, 31, 18, 90, back=True, pads={'1': 'AMP_INN', '2': 'GND'}, value='1uF')
part('U3', 'Package_SO', 'MSOP-8_3x3mm_P0.65mm', 30, 12, 0, back=True,
     pads={'1': 'AMP_EN', '3': 'AMP_INP', '4': 'AMP_INN', '5': 'SPK_P', '6': 'SYS_5V', '7': 'GND', '8': 'SPK_N'},
     value='PAM8302A', mpn='Diodes PAM8302AAYCR')
part('C13', *C0805, 34, 14, 90, back=True, pads={'1': 'SYS_5V', '2': 'GND'}, value='10uF')
part('R11', *R0603, 27, 8, 0, back=True, pads={'1': 'AMP_EN', '2': 'GND'}, value='100k')
part('J7', 'Connector_JST', 'JST_PH_S2B-PH-K_1x02_P2.00mm_Horizontal', 74, 8, 90, back=True,
     pads={'1': 'SPK_P', '2': 'SPK_N'}, value='Speaker 8R 1W', mpn='20 mm 8 ohm speaker')
part('J8', 'Connector_Audio', 'Jack_3.5mm_CUI_SJ1-3523N_Horizontal', 16, 5, 180, back=True,
     pads={'T': 'HP_OUT', 'R': 'HP_OUT', 'S': 'GND'}, value='3.5 mm', mpn='CUI SJ1-3523N')
part('R12', *R0603, 20, 10, 90, back=True, pads={'1': 'AUDIO', '2': 'HP_OUT'}, value='100R')

# link port (back, top edge) with BSS138 level shifters (3.3 V FPGA side, 5 V Game Boy side)
part('J9', 'custom', link_port, 60, 132, 180, back=True,
     pads={'1': 'SYS_5V', '2': 'LINK_SO_5V', '3': 'LINK_SI_5V', '5': 'LINK_SC_5V', '6': 'GND'},
     value='Link port', mpn='Game Boy Color link port socket')
for i, (sig, x) in enumerate([('SO', 52), ('SI', 60), ('SC', 68)]):
    part('Q%d' % (i + 1), *SOT23, x, 122, 0, back=True, pads={'1': '+3V3', '2': 'LINK_%s_3V' % sig, '3': 'LINK_%s_5V' % sig}, value='BSS138')
    part('R%d' % (30 + i * 2), *R0603, x - 2.5, 117, 90, back=True, pads={'1': 'LINK_%s_3V' % sig, '2': '+3V3'}, value='10k')
    part('R%d' % (31 + i * 2), *R0603, x + 2.5, 117, 90, back=True, pads={'1': 'LINK_%s_5V' % sig, '2': 'SYS_5V'}, value='10k')

# W25Q "cartridge" flash (back)
part('U4', 'Package_SO', 'SOIC-8_5.3x5.3mm_P1.27mm', 32, 116, 0, back=True,
     pads={'1': 'CART_CS', '2': 'CART_DO', '3': '+3V3', '4': 'GND', '5': 'CART_DI', '6': 'CART_CLK', '7': '+3V3', '8': '+3V3'},
     value='W25Q128JV', mpn='Winbond W25Q128JVSIQ')
part('C20', *C0603, 38, 116, 90, back=True, pads={'1': '+3V3', '2': 'GND'}, value='100nF')

# mounting holes (M2, the case screws)
for i, (x, y) in enumerate([(6, 6), (W - 6, 6), (6, H - 6), (W - 6, H - 6)]):
    part('H%d' % (i + 1), 'MountingHole', 'MountingHole_2.2mm_M2', x, y, value='M2')

# ---------------------------------------------------------------- ground pours and text
def zone(layer):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNet(net('GND'))
    z.SetLocalClearance(mm(0.3))
    z.SetMinThickness(mm(0.25))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    ol = z.Outline()
    ol.NewOutline()
    for (x, y) in [(0.5, 0.5), (W - 0.5, 0.5), (W - 0.5, H - 0.5), (0.5, H - 0.5)]:
        ol.Append(mm(OX + x), mm(OY + H - y))
    board.Add(z)


zone(pcbnew.F_Cu)
zone(pcbnew.B_Cu)

for txt, (x, y), layer, size in [('OPEN POCKET GBC', (43, 72), pcbnew.F_SilkS, 2.0), ('carrier v0.1', (43, 69), pcbnew.F_SilkS, 1.0),
                                 ('OPEN POCKET GBC v0.1', (43, 80), pcbnew.B_SilkS, 1.6)]:
    t = pcbnew.PCB_TEXT(board)
    t.SetText(txt)
    t.SetPosition(P(x, y))
    t.SetLayer(layer)
    t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
    if layer == pcbnew.B_SilkS:
        t.SetMirrored(True)
    board.Add(t)

# design rules for a cheap 2-layer order
ds = board.GetDesignSettings()
ds.SetCopperLayerCount(2)
ds.m_TrackMinWidth = mm(0.15)
ds.m_MinClearance = mm(0.15)
ds.m_ViasMinSize = mm(0.6)
ds.m_MinThroughDrill = mm(0.3)

out = os.path.join(HERE, 'open_pocket.kicad_pcb')
board.Save(out)
with open(os.path.join(HERE, 'bom.csv'), 'w', newline='') as f:
    w = csv.writer(f)
    w.writerow(['Ref', 'Value', 'Footprint', 'Part', 'Side'])
    for row in sorted(bom, key=lambda r: (r[0].rstrip('0123456789'), int(''.join(c for c in r[0] if c.isdigit()) or 0))):
        w.writerow(row)
print('wrote', out, '-', len(bom), 'parts,', len(nets), 'nets')
