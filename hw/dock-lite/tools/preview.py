#!/usr/bin/env python3
"""Rough PNG preview of the generated schematic (not KiCad's renderer)."""
import json, sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
g = json.load(open(ROOT / 'build_geom.json'))
S = 6  # px per mm
img = Image.new('RGB', (420 * S, 297 * S), 'white')
d = ImageDraw.Draw(img)
try:
    fnt = ImageFont.truetype('DejaVuSans.ttf', int(1.4 * S)); big = ImageFont.truetype('DejaVuSans-Bold.ttf', int(2.2 * S))
except OSError:
    fnt = big = ImageFont.load_default()
P = lambda x, y: (x * S, y * S)
for x0, y0, x1, y1, t in g['blocks']:
    d.rectangle([*P(x0, y0), *P(x1, y1)], outline=(150, 150, 150))
    d.text(P(x0 + 2, y0 + 2), t, fill=(120, 0, 0), font=big)
for x, y, t in g['notes']:
    d.multiline_text(P(x, y), t, fill=(0, 0, 120), font=fnt)
for c in g['comps']:
    s = g['syms'][c['sym']]; X, Y = c['x'], c['y']
    bx0, by0, bx1, by1 = s['box']
    d.rectangle([*P(X + bx0 + 2.54, Y - by1), *P(X + bx1 - 2.54, Y - by0)], outline='black', width=2)
    d.text(P(X - 2, Y - by1 - 2.2), c['ref'], fill='black', font=fnt)
    d.text(P(X + 7, Y + 1.5) if c['sym'] == 'POT' else P(X - 2, Y - by0 + 0.6), c['val'], fill=(0, 100, 0), font=fnt)
    for num, nm, px, py, a in s['pins']:
        ax, ay = X + px, Y - py
        net = c['nets'][num]
        dx, dy = {0: (-2.54, 0), 180: (2.54, 0), 90: (0, 2.54), 270: (0, -2.54)}[a]
        ix, iy = {0: (2.54, 0), 180: (-2.54, 0), 90: (0, -2.54), 270: (0, 2.54)}[a]
        d.line([*P(ax, ay), *P(ax + ix, ay + iy)], fill='black', width=2)
        if nm: d.text(P(ax + ix + (0.5 if a == 0 else -0.5 - len(nm) * 1.0), ay - 0.8), nm, fill=(90, 90, 90), font=fnt)
        if net is None:
            d.line([*P(ax - 0.8, ay - 0.8), *P(ax + 0.8, ay + 0.8)], fill='blue', width=2)
            d.line([*P(ax - 0.8, ay + 0.8), *P(ax + 0.8, ay - 0.8)], fill='blue', width=2)
            continue
        ex, ey = ax + dx, ay + dy
        d.line([*P(ax, ay), *P(ex, ey)], fill=(0, 140, 0), width=2)
        w = d.textlength(net, font=fnt) / S
        tx = ex - w if a == 0 else (ex if a == 180 else ex - 0.7)
        ty = ey - 1.7 if a in (0, 180) else ey
        d.text(P(tx, ty), net, fill=(160, 0, 160), font=fnt)
img.save(sys.argv[1] if len(sys.argv) > 1 else ROOT / 'preview.png')
