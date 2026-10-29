#!/usr/bin/env python3
"""Generates namfx_dock_lite.kicad_sch (KiCad 8 format) + BOM + netlist.

Connections are made with net labels on short wire stubs at every pin, so the
whole design is defined by the COMPONENTS table below (single source of truth).
"""
import csv, uuid, sys
from collections import defaultdict
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent
PROJECT = 'namfx_dock_lite'
NS = uuid.UUID('6f1c1a52-6a1d-4b7e-9d8a-2f6e0c3b9a10')
def uid(*k): return str(uuid.uuid5(NS, '/'.join(map(str, k))))
ROOT = uid('root')
G = 1.27
def f(v): return f'{round(v, 4):g}'

# ---------------------------------------------------------------- symbols
# pin: (number, name, x, y, angle)   angle 0 = pin on the left side (points right)
SYMS = {}

def two_pin(name, ref, gfx, left='1', right='2'):
    SYMS[name] = dict(ref=ref, pins=[(left, '', -5.08, 0, 0), (right, '', 5.08, 0, 180)],
                      gfx=gfx, names=False, box=(-5.08, -1.27, 5.08, 1.27))

def rect(x0, y0, x1, y1, fill='none'):
    return f'(rectangle (start {f(x0)} {f(y0)}) (end {f(x1)} {f(y1)}) (stroke (width 0.254) (type default)) (fill (type {fill})))'
def poly(pts, fill='none'):
    p = ' '.join(f'(xy {f(x)} {f(y)})' for x, y in pts)
    return f'(polyline (pts {p}) (stroke (width 0.254) (type default)) (fill (type {fill})))'

two_pin('R', 'R', [rect(-2.54, -1.016, 2.54, 1.016)])
two_pin('C', 'C', [poly([(-0.508, -1.778), (-0.508, 1.778)]), poly([(0.508, -1.778), (0.508, 1.778)]),
                   poly([(-2.54, 0), (-0.508, 0)]), poly([(0.508, 0), (2.54, 0)])])
two_pin('FB', 'FB', [rect(-2.54, -0.889, 2.54, 0.889, 'outline')])
two_pin('Fuse', 'F', [rect(-2.54, -1.016, 2.54, 1.016), poly([(-2.54, 0), (2.54, 0)])])
_d = [poly([(-1.27, 1.27), (-1.27, -1.27), (1.27, 0), (-1.27, 1.27)]), poly([(1.27, 1.27), (1.27, -1.27)]),
      poly([(-2.54, 0), (-1.27, 0)]), poly([(1.27, 0), (2.54, 0)])]
two_pin('D', 'D', _d, left='2', right='1')                       # pin1 = K (right), pin2 = A (left)
two_pin('D_TVS', 'D', _d[:1] + [poly([(0.762, 1.524), (1.27, 1.27), (1.27, -1.27), (1.778, -1.524)])] + _d[2:],
        left='2', right='1')
SYMS['POT'] = dict(ref='RV', pins=[('1', '', -5.08, 0, 0), ('3', '', 5.08, 0, 180), ('2', '', 0, -5.08, 90)],
                   gfx=[rect(-2.54, -1.016, 2.54, 1.016), poly([(0, -2.54), (0, -1.27)]),
                        poly([(-0.635, -1.905), (0, -1.27), (0.635, -1.905)])],
                   names=False, box=(-5.08, -5.08, 5.08, 2.54))

def box(name, ref, left, right, hw):
    n = max(len(left), len(right))
    y0 = (n - 1) * G
    pins = []
    for i, (num, nm) in enumerate(left):
        pins.append((num, nm, -hw - 2.54, y0 - i * 2.54, 0))
    for i, (num, nm) in enumerate(right):
        pins.append((num, nm, hw + 2.54, y0 - i * 2.54, 180))
    top, bot = y0 + 2.54, y0 - (n - 1) * 2.54 - 2.54
    SYMS[name] = dict(ref=ref, pins=pins, gfx=[rect(-hw, top, hw, bot, 'background')], names=True,
                      box=(-hw - 2.54, bot, hw + 2.54, top))

box('USB_C_16P', 'J', [('A4', 'VBUS'), ('A9', 'VBUS'), ('B4', 'VBUS'), ('B9', 'VBUS'), ('A5', 'CC1'), ('B5', 'CC2'),
                       ('A6', 'D+'), ('B6', 'D+'), ('A7', 'D-'), ('B7', 'D-'), ('A8', 'SBU1'), ('B8', 'SBU2')],
    [('A1', 'GND'), ('A12', 'GND'), ('B1', 'GND'), ('B12', 'GND'), ('S1', 'SHIELD')], 7.62)
box('USBLC6-2SC6', 'U', [('1', 'I/O1'), ('2', 'GND'), ('3', 'I/O2')], [('6', 'I/O1'), ('5', 'VBUS'), ('4', 'I/O2')], 5.08)
box('TUSB320LAI', 'U', [('1', 'CC1'), ('2', 'CC2'), ('3', 'PORT'), ('4', 'VBUS_DET'), ('5', 'ADDR'), ('6', 'OUT3')],
    [('12', 'VDD'), ('11', 'EN_N'), ('10', 'GND'), ('9', 'ID'), ('8', 'OUT2'), ('7', 'OUT1')], 8.89)
box('AO4805', 'Q', [('1', 'S1'), ('2', 'G1'), ('3', 'S2'), ('4', 'G2')], [('8', 'D1'), ('7', 'D1'), ('6', 'D2'), ('5', 'D2')], 5.08)
box('NMOS', 'Q', [('1', 'G')], [('3', 'D'), ('2', 'S')], 2.54)
box('BAT54C', 'D', [('1', 'A1'), ('2', 'A2')], [('3', 'K')], 2.54)
box('BAV99', 'D', [('3', 'COM')], [('2', 'K'), ('1', 'A')], 3.81)
box('LP5907', 'U', [('1', 'IN'), ('3', 'EN'), ('2', 'GND')], [('5', 'OUT'), ('4', 'NC')], 5.08)
box('MCP6022', 'U', [('3', '+A'), ('2', '-A'), ('1', 'OUTA'), ('4', 'VSS')], [('8', 'VDD'), ('5', '+B'), ('6', '-B'), ('7', 'OUTB')], 5.08)
for n, names in {2: ['1', '2'], 3: ['1', '2', '3'], 4: ['1', '2', '3', '4'], 5: ['1', '2', '3', '4', '5']}.items():
    pass
def conn(name, pinnames):
    box(name, 'J', [], [(str(i + 1), nm) for i, nm in enumerate(pinnames)], 5.08)

# ---------------------------------------------------------------- design
FP = dict(R='Resistor_SMD:R_0603_1608Metric', C='Capacitor_SMD:C_0603_1608Metric',
          C0805='Capacitor_SMD:C_0805_2012Metric', C1206='Capacitor_SMD:C_1206_3216Metric',
          FB='Inductor_SMD:L_0603_1608Metric', SOT23='Package_TO_SOT_SMD:SOT-23',
          SO8='Package_SO:SOIC-8_3.9x4.9mm_P1.27mm')
def hdr(n): return f'Connector_PinHeader_2.54mm:PinHeader_1x{n:02d}_P2.54mm_Vertical'

conn('CONN_OTG', ['VBUS', 'D+', 'D-', 'CC', 'GND'])
conn('CONN_CHG', ['VBUS', 'CC', 'GND'])
conn('CONN_TRRS', ['T (L)', 'R1 (R)', 'R2 (MIC)', 'S (GND)'])
conn('CONN_GTR', ['TIP', 'SLEEVE'])
conn('CONN_HP', ['T (L)', 'R (R)', 'S (GND)'])

NC = None
# ref, symbol, value, footprint, mpn, x, y, {pin: net}, note
C = []
def add(ref, sym, val, fp, x, y, nets, mpn='', note=''):
    C.append(dict(ref=ref, sym=sym, val=val, fp=fp, x=x, y=y, nets=nets, mpn=mpn, note=note))

# ---- block 1: external USB-C port (DRP)
add('J1', 'USB_C_16P', 'USB-C EXT (DRP)', 'Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12', 45.72, 68.58,
    {'A4': 'EXT_VBUS', 'A9': 'EXT_VBUS', 'B4': 'EXT_VBUS', 'B9': 'EXT_VBUS', 'A5': 'CC1', 'B5': 'CC2',
     'A6': 'USB_DP', 'B6': 'USB_DP', 'A7': 'USB_DM', 'B7': 'USB_DM', 'A8': NC, 'B8': NC,
     'A1': 'GND', 'A12': 'GND', 'B1': 'GND', 'B12': 'GND', 'S1': 'GND'}, mpn='HRO TYPE-C-31-M-12')
add('U4', 'USBLC6-2SC6', 'USBLC6-2SC6', 'Package_TO_SOT_SMD:SOT-23-6', 45.72, 109.22,
    {'1': 'USB_DP', '6': 'USB_DP', '3': 'USB_DM', '4': 'USB_DM', '2': 'GND', '5': 'EXT_VBUS'}, mpn='USBLC6-2SC6')
add('D1', 'D_TVS', 'SMF5.0A', 'Diode_SMD:D_SOD-123F', 100.33, 40.64, {'1': 'EXT_VBUS', '2': 'GND'}, mpn='SMF5.0A')
add('C1', 'C', '4.7u 16V', FP['C0805'], 100.33, 50.8, {'1': 'EXT_VBUS', '2': 'GND'}, note='max 10 uF (Type-C sink)')
add('U2', 'TUSB320LAI', 'TUSB320LAI', 'Package_DFN_QFN:Texas_X2QFN-12_1.6x1.6mm_P0.4mm', 106.68, 76.2,
    {'1': 'CC1', '2': 'CC2', '3': NC, '4': 'VBUS_DET', '5': NC, '6': NC,
     '12': 'VDOCK', '11': 'GND', '10': 'GND', '9': 'ID', '8': NC, '7': NC},
    mpn='TUSB320LAIRWBR', note='PORT NC = DRP, ADDR NC = GPIO mode')
add('R1', 'R', '887k 1%', FP['R'], 100.33, 99.06, {'1': 'EXT_VBUS', '2': 'VBUS_DET'})
add('R2', 'R', '200k', FP['R'], 100.33, 109.22, {'1': 'VDOCK', '2': 'ID'})
add('C2', 'C', '100n', FP['C'], 100.33, 119.38, {'1': 'VDOCK', '2': 'GND'})
add('D2', 'BAT54C', 'BAT54C', FP['SOT23'], 100.33, 132.08, {'1': 'OTG_VBUS', '2': 'EXT_VBUS', '3': 'VDOCK'}, mpn='BAT54C')
add('C3', 'C', '10u 10V', FP['C0805'], 100.33, 144.78, {'1': 'VDOCK', '2': 'GND'})

# ---- block 2: switch A (charger -> console charging port)
add('Q1', 'AO4805', 'AO4805', FP['SO8'], 172.72, 50.8,
    {'1': 'SWA_S', '3': 'SWA_S', '2': 'SWA_G', '4': 'SWA_G', '8': 'EXT_VBUS', '7': 'EXT_VBUS', '6': 'CHG_SW', '5': 'CHG_SW'},
    mpn='AO4805', note='dual P-MOSFET, back-to-back (common source)')
add('R3', 'R', '100k', FP['R'], 220.98, 40.64, {'1': 'SWA_S', '2': 'SWA_G'})
add('Q3', 'NMOS', '2N7002', FP['SOT23'], 220.98, 55.88, {'1': 'A_EN', '3': 'SWA_G', '2': 'GND'}, mpn='2N7002')
add('R4', 'R', '100k', FP['R'], 172.72, 73.66, {'1': 'EXT_VBUS', '2': 'A_EN'})
add('R5', 'R', '220k', FP['R'], 172.72, 83.82, {'1': 'A_EN', '2': 'GND'})
add('D3', 'D', 'BAT54WS', 'Diode_SMD:D_SOD-323', 220.98, 71.12, {'2': 'A_EN', '1': 'ID'}, mpn='BAT54WS', note='ID low -> A off')
add('F1', 'Fuse', '3A PTC', 'Fuse:Fuse_1812_4532Metric', 220.98, 86.36, {'1': 'CHG_SW', '2': 'CHG_VBUS'})
add('C4', 'C', '22u 10V', FP['C0805'], 172.72, 93.98, {'1': 'CHG_VBUS', '2': 'GND'})
add('R6', 'R', '56k', FP['R'], 220.98, 96.52, {'1': 'CHG_VBUS', '2': 'CHG_CC'}, note='Rp default USB')

# ---- block 3: switch B (console OTG -> device on EXT)
add('Q2', 'AO4805', 'AO4805', FP['SO8'], 172.72, 119.38,
    {'1': 'SWB_S', '3': 'SWB_S', '2': 'SWB_G', '4': 'SWB_G', '8': 'OTG_VBUS_F', '7': 'OTG_VBUS_F', '6': 'EXT_VBUS', '5': 'EXT_VBUS'},
    mpn='AO4805', note='dual P-MOSFET, back-to-back (common source)')
add('R7', 'R', '100k', FP['R'], 220.98, 109.22, {'1': 'SWB_S', '2': 'SWB_G'})
add('Q4', 'NMOS', '2N7002', FP['SOT23'], 220.98, 124.46, {'1': 'B_EN', '3': 'SWB_G', '2': 'GND'}, mpn='2N7002')
add('Q5', 'NMOS', '2N7002', FP['SOT23'], 172.72, 142.24, {'1': 'ID', '3': 'B_EN', '2': 'GND'}, mpn='2N7002', note='ID inverter')
add('R8', 'R', '100k', FP['R'], 220.98, 139.7, {'1': 'VDOCK', '2': 'B_EN'})
add('F2', 'Fuse', '1A PTC', 'Fuse:Fuse_1206_3216Metric', 220.98, 149.86, {'1': 'OTG_VBUS', '2': 'OTG_VBUS_F'})
add('C5', 'C', '22u 10V', FP['C0805'], 172.72, 157.48, {'1': 'OTG_VBUS', '2': 'GND'})

# ---- block 4: console pigtails
add('J2', 'CONN_OTG', 'Console OTG (USB-C plug pigtail)', hdr(5), 287.02, 50.8,
    {'1': 'OTG_VBUS', '2': 'USB_DP', '3': 'USB_DM', '4': 'OTG_CC', '5': 'GND'})
add('R9', 'R', '5.1k', FP['R'], 337.82, 45.72, {'1': 'OTG_CC', '2': 'GND'}, note='Rd: console = host')
add('J3', 'CONN_CHG', 'Console charging (USB-C plug pigtail)', hdr(3), 287.02, 83.82,
    {'1': 'CHG_VBUS', '2': 'CHG_CC', '3': 'GND'})

# ---- block 5: analog supply + VREF
add('FB1', 'FB', '600R@100MHz', FP['FB'], 287.02, 129.54, {'1': 'VDOCK', '2': 'VDOCK_F'})
add('C6', 'C', '1u', FP['C'], 287.02, 139.7, {'1': 'VDOCK_F', '2': 'GND'})
add('U3', 'LP5907', 'LP5907MFX-3.3', 'Package_TO_SOT_SMD:SOT-23-5', 340.36, 132.08,
    {'1': 'VDOCK_F', '3': 'VDOCK_F', '2': 'GND', '5': '3V3A', '4': NC}, mpn='LP5907MFX-3.3/NOPB')
add('C7', 'C', '1u', FP['C'], 287.02, 149.86, {'1': '3V3A', '2': 'GND'})
add('FB2', 'FB', '600R@100MHz', FP['FB'], 340.36, 152.4, {'1': 'GND', '2': 'AGND'}, note='single GND-AGND tie, place at TRRS sleeve')
add('C8', 'C', '10u 10V', FP['C0805'], 287.02, 160.02, {'1': '3V3A', '2': 'AGND'})
add('R10', 'R', '100k', FP['R'], 287.02, 175.26, {'1': '3V3A', '2': 'VREF'})
add('R11', 'R', '100k', FP['R'], 287.02, 185.42, {'1': 'VREF', '2': 'AGND'})
add('C10', 'C', '22u 10V', FP['C0805'], 340.36, 175.26, {'1': 'VREF', '2': 'AGND'})
add('C9', 'C', '100n', FP['C'], 340.36, 185.42, {'1': '3V3A', '2': 'AGND'}, note='at U1 pin 8')

# ---- block 6: guitar buffer
add('J4', 'CONN_GTR', 'Guitar 6.35 mm jack', hdr(2), 30.48, 215.9, {'1': 'GTR_TIP', '2': 'AGND'})
add('R12', 'R', '2.2M', FP['R'], 76.2, 205.74, {'1': 'GTR_TIP', '2': 'AGND'})
add('C11', 'C', '100n film', FP['C1206'], 76.2, 215.9, {'1': 'GTR_TIP', '2': 'IN_AC'}, note='PPS/PEN film')
add('R13', 'R', '1k', FP['R'], 76.2, 228.6, {'1': 'IN_AC', '2': 'IN_P'})
add('R14', 'R', '1M', FP['R'], 76.2, 238.76, {'1': 'IN_P', '2': 'VREF'})
add('C12', 'C', '100p C0G', FP['C'], 76.2, 248.92, {'1': 'IN_P', '2': 'AGND'})
add('D4', 'BAV99', 'BAV99', FP['SOT23'], 76.2, 261.62, {'3': 'IN_P', '2': '3V3A', '1': 'AGND'}, mpn='BAV99')
add('U1', 'MCP6022', 'MCP6022', FP['SO8'], 132.08, 223.52,
    {'3': 'IN_P', '2': 'BUF1', '1': 'BUF1', '4': 'AGND', '8': '3V3A', '5': 'BUF1', '6': 'FB_N', '7': 'AMP_OUT'},
    mpn='MCP6022-E/SN', note='A: follower, B: gain 2-12x')
add('R15', 'R', '1k', FP['R'], 187.96, 205.74, {'1': 'AMP_OUT', '2': 'RF_MID'})
add('RV1', 'POT', '10k GAIN', 'Potentiometer_SMD:Potentiometer_Bourns_3314J_Vertical', 187.96, 218.44,
    {'1': 'RF_MID', '2': 'FB_N', '3': 'FB_N'}, mpn='Bourns 3314J-1-103E', note='rheostat: gain 2-12x')
add('C13', 'C', '680p C0G', FP['C'], 187.96, 236.22, {'1': 'AMP_OUT', '2': 'FB_N'})
add('R16', 'R', '1k', FP['R'], 187.96, 246.38, {'1': 'FB_N', '2': 'RG_C'})
add('C14', 'C', '10u 10V', FP['C0805'], 187.96, 256.54, {'1': 'RG_C', '2': 'AGND'})
add('C15', 'C', '10u 16V X7R', FP['C1206'], 241.3, 205.74, {'1': 'AMP_OUT', '2': 'OUT_C'}, note='non-polar output coupling')
add('R17', 'R', '100R', FP['R'], 241.3, 218.44, {'1': 'OUT_C', '2': 'MIC'})
add('C16', 'C', '1n C0G', FP['C'], 241.3, 228.6, {'1': 'MIC', '2': 'AGND'})

# ---- block 7: audio connectors
add('J5', 'CONN_TRRS', 'Console 3.5 mm TRRS (OMTP) pigtail', hdr(4), 302.26, 215.9,
    {'1': 'HP_L', '2': 'HP_R', '3': 'MIC', '4': 'AGND'})
add('J6', 'CONN_HP', 'Headphones 3.5 mm TRS', hdr(3), 365.76, 215.9, {'1': 'HP_L', '2': 'HP_R', '3': 'AGND'})

BLOCKS = [
    (15, 25, 140, 165, '1  External USB-C port, dual role (DRP)'),
    (148, 25, 262, 165, '2  Switch A: charger -> console   /   3  Switch B: console OTG -> device'),
    (270, 25, 405, 105, '4  Console pigtails (USB-C plugs)'),
    (270, 112, 405, 195, '5  Analog supply, VREF, ground tie'),
    (15, 190, 262, 275, '6  Guitar buffer (Hi-Z in, gain 2-12x, out to console MIC)'),
    (270, 200, 405, 275, '7  Audio connectors'),
]
NOTES = [
    (150, 172, 'EXT port logic (TUSB320LAI, GPIO mode, DRP):\n'
               ' charger attached  -> CC sees Rp -> UFP -> ID high-Z -> A_EN = VBUS divider -> A on (charges console)\n'
               ' device attached   -> CC sees Rd -> DFP -> ID low   -> B_EN high -> B on (console OTG powers device); A forced off via D3\n'
               ' dead console      -> U2 unpowered presents Rd -> charger gives VBUS -> A on, VDOCK via D2\n'
               'Never connect OTG_VBUS to CHG_VBUS. 5 V only (no PD).'),
]

# ---------------------------------------------------------------- emit
def lib_symbol(name, s):
    out = [f'(symbol "dock:{name}"']
    if not s['names']:
        out.append('(pin_numbers hide) (pin_names (offset 0) hide)')
    else:
        out.append('(pin_names (offset 0.508))')
    out.append('(exclude_from_sim no) (in_bom yes) (on_board yes)')
    x0, y0, x1, y1 = s['box']
    for k, v, y, hide in [('Reference', s['ref'], y1 + 1.27, False), ('Value', name, y0 - 1.27, False),
                          ('Footprint', '', 0, True), ('Datasheet', '', 0, True), ('Description', '', 0, True)]:
        out.append(f'(property "{k}" "{v}" (at 0 {f(y)} 0) (effects (font (size 1.27 1.27)){" hide" if hide else ""}))')
    out.append(f'(symbol "{name}_0_1" ' + ' '.join(s['gfx']) + ')')
    pins = []
    for num, nm, x, y, a in s['pins']:
        pins.append(f'(pin passive line (at {f(x)} {f(y)} {a}) (length 2.54) '
                    f'(name "{nm or "~"}" (effects (font (size 1.27 1.27)))) (number "{num}" (effects (font (size 1.27 1.27)))))')
    out.append(f'(symbol "{name}_1_1" ' + ' '.join(pins) + ')')
    out.append(')')
    return ' '.join(out)

def esc(s): return s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n')

items, nets, errors = [], defaultdict(list), []
bboxes = []
for c in C:
    s = SYMS[c['sym']]
    X, Y = c['x'], c['y']
    if abs(X / G - round(X / G)) > 1e-6 or abs(Y / G - round(Y / G)) > 1e-6:
        errors.append(f"{c['ref']} off grid")
    pinnums = [p[0] for p in s['pins']]
    if set(pinnums) != set(c['nets']):
        errors.append(f"{c['ref']}: pins {sorted(pinnums)} vs nets {sorted(c['nets'])}")
    x0, y0, x1, y1 = s['box']
    vx, vy = (X + 8.89, Y + 2.54) if c['sym'] == 'POT' else (X, Y - y0 + 1.27)
    props = [('Reference', c['ref'], X, Y - y1 - 1.27, False), ('Value', c['val'], vx, vy, False),
             ('Footprint', c['fp'], X, Y, True), ('Datasheet', '', X, Y, True), ('MPN', c['mpn'], X, Y, True)]
    if c['note']:
        props.append(('Note', c['note'], X, Y - y0 + 3.81, False))
    p = []
    for k, v, px, py, hide in props:
        p.append(f'(property "{k}" "{esc(v)}" (at {f(px)} {f(py)} 0) (effects (font (size 1.27 1.27)){" hide" if hide else ""}))')
    pinuu = ' '.join(f'(pin "{n}" (uuid "{uid(c["ref"], "pin", n)}"))' for n in pinnums)
    items.append(f'(symbol (lib_id "dock:{c["sym"]}") (at {f(X)} {f(Y)} 0) (unit 1) (exclude_from_sim no) (in_bom yes) '
                 f'(on_board yes) (dnp no) (uuid "{uid(c["ref"])}") {" ".join(p)} {pinuu} '
                 f'(instances (project "{PROJECT}" (path "/{ROOT}" (reference "{c["ref"]}") (unit 1)))))')
    lw = []
    for num, nm, px, py, a in s['pins']:
        ax, ay = X + px, Y - py
        net = c['nets'][num]
        if net is None:
            items.append(f'(no_connect (at {f(ax)} {f(ay)}) (uuid "{uid(c["ref"], "nc", num)}"))')
            continue
        nets[net].append(f"{c['ref']}.{num}")
        dx, dy, la, just = {0: (-2.54, 0, 180, 'right bottom'), 180: (2.54, 0, 0, 'left bottom'),
                            90: (0, 2.54, 270, 'right bottom'), 270: (0, -2.54, 90, 'left bottom')}[a]
        ex, ey = ax + dx, ay + dy
        items.append(f'(wire (pts (xy {f(ax)} {f(ay)}) (xy {f(ex)} {f(ey)})) (stroke (width 0) (type default)) '
                     f'(uuid "{uid(c["ref"], "w", num)}"))')
        items.append(f'(label "{net}" (at {f(ex)} {f(ey)} {la}) (fields_autoplaced yes) '
                     f'(effects (font (size 1.27 1.27)) (justify {just})) (uuid "{uid(c["ref"], "l", num)}"))')
        tl = len(net) * 1.05 + 0.6
        if a == 0: lw.append((ex - tl, ey - 1.5, ex, ey + 0.3))
        elif a == 180: lw.append((ex, ey - 1.5, ex + tl, ey + 0.3))
        else: lw.append((ex - 1.5, ey, ex + 0.3, ey + tl))
    bx = [X + x0, Y - y1 - 2.6, X + x1, Y - y0 + (5.2 if c['note'] else 2.6)]
    for r in lw:
        bx = [min(bx[0], r[0]), min(bx[1], r[1]), max(bx[2], r[2]), max(bx[3], r[3])]
    bboxes.append((c['ref'], bx))

for i in range(len(bboxes)):
    for j in range(i + 1, len(bboxes)):
        a, b = bboxes[i][1], bboxes[j][1]
        if a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]:
            errors.append(f'overlap {bboxes[i][0]} / {bboxes[j][0]}')
for net, pins in nets.items():
    if len(pins) < 2:
        errors.append(f'single-pin net {net}: {pins}')

for i, (x0, y0, x1, y1, title) in enumerate(BLOCKS):
    items.append(f'(rectangle (start {f(x0)} {f(y0)}) (end {f(x1)} {f(y1)}) (stroke (width 0.2) (type dash)) '
                 f'(fill (type none)) (uuid "{uid("blk", i)}"))')
    items.append(f'(text "{esc(title)}" (exclude_from_sim no) (at {f(x0 + 2)} {f(y0 + 5)} 0) '
                 f'(effects (font (size 2 2) (thickness 0.4) bold) (justify left bottom)) (uuid "{uid("blkt", i)}"))')
for i, (x, y, t) in enumerate(NOTES):
    items.append(f'(text "{esc(t)}" (exclude_from_sim no) (at {x} {y} 0) '
                 f'(effects (font (size 1.27 1.27)) (justify left top)) (uuid "{uid("note", i)}"))')

used = sorted({c['sym'] for c in C})
sch = (f'(kicad_sch (version 20231120) (generator "eeschema") (generator_version "8.0") (uuid "{ROOT}") (paper "A3") '
       f'(title_block (title "namfx dock - Lite (no hub, no WiFi)") (date "2026-10-27") (rev "0.1") '
       f'(company "namfx") (comment 1 "Guitar buffer + DRP USB-C (charge or device) for R36S")) '
       f'(lib_symbols {" ".join(lib_symbol(n, SYMS[n]) for n in used)}) '
       + '\n'.join(items) +
       f' (sheet_instances (path "/" (page "1"))))\n')

# paren balance sanity
depth, instr, prev = 0, False, ''
for ch in sch:
    if ch == '"' and prev != '\\': instr = not instr
    elif not instr:
        if ch == '(': depth += 1
        elif ch == ')': depth -= 1
        if depth < 0: break
    prev = ch if not (prev == '\\' and ch == '\\') else ''
if depth != 0: errors.append(f'paren balance {depth}')

if errors:
    print('\n'.join(errors)); sys.exit(1)

(OUT / f'{PROJECT}.kicad_sch').write_text(sch, encoding='utf-8')
(OUT / f'{PROJECT}.kicad_pro').write_text(
    '{\n  "meta": {"filename": "%s.kicad_pro", "version": 1},\n  "schematic": {"legacy_lib_dir": "", "legacy_lib_list": []},\n'
    '  "sheets": [["%s", ""]]\n}\n' % (PROJECT, ROOT), encoding='utf-8')
with open(OUT / 'netlist.txt', 'w') as fh:
    for net in sorted(nets):
        fh.write(f'{net:12s} {" ".join(sorted(nets[net]))}\n')
groups = defaultdict(list)
for c in C:
    groups[(c['val'], c['fp'], c['mpn'])].append(c['ref'])
def refkey(r):
    import re
    m = re.match(r'([A-Z]+)(\d+)', r); return (m.group(1), int(m.group(2)))
with open(OUT / 'bom.csv', 'w', newline='') as fh:
    w = csv.writer(fh)
    w.writerow(['Qty', 'References', 'Value', 'Footprint', 'MPN'])
    for (val, fp, mpn), refs in sorted(groups.items(), key=lambda kv: refkey(sorted(kv[1], key=refkey)[0])):
        w.writerow([len(refs), ' '.join(sorted(refs, key=refkey)), val, fp, mpn])
# geometry dump for the preview renderer
import json
json.dump(dict(syms={k: dict(pins=v['pins'], box=v['box']) for k, v in SYMS.items()},
               comps=C, blocks=BLOCKS, notes=NOTES), open(OUT / 'build_geom.json', 'w'))
print(f'OK: {len(C)} components, {len(nets)} nets')
