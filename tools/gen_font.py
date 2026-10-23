#!/usr/bin/env python3
"""Rasterise the sign's fonts into src/fonts/StopFonts.h.

Latin letters (A-Z, a-z) come from High-School by GeronimoFonts
(tools/fonts/High-School.ttf, a FontStruct font under the FontStruct
Non-Commercial License). It is unicase: lower case draws the capitals. It is
scaled per size so its capitals stand exactly as tall as DejaVu Sans Bold's,
on the same baseline. Digits, punctuation, the space and the symbols stay
DejaVu Sans Bold (High-School has none of them). Hebrew letters
(U+05D0-U+05EA, geresh, gershayim) come from Heshbon by AlefAlefAlef
(tools/fonts/heshbon-bold-alefalefalef.otf); any of them the .otf lacks
falls back to DejaVu. Heshbon is scaled per size so its letters stand as tall
as DejaVu's capitals and is shifted so its letters sit on the same baseline;
the line box (ascent/descent) stays DejaVu's.

High-School's K and X differ from H, R from A and V from U only by small
corner cuts, which vanish at these pixel sizes ("FOX" read "FOH", "OVER"
"OUEA"). Those four (and k, r, v, x) are redrawn here instead, in its style:
the same height, width and stroke as its H, with real diagonals (redrawn()).

Format (see src/Font.h): 1 bpp glyph bitmaps, rows padded to whole bytes, MSB
first. yOff is measured from the top of the line box (ascender line).
"""
from PIL import Image, ImageDraw, ImageFont
import os, statistics

HERE = os.path.dirname(os.path.abspath(__file__))
TTF = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
HEB_FONT = os.path.join(HERE, "fonts", "heshbon-bold-alefalefalef.otf")
LAT_FONT = os.path.join(HERE, "fonts", "High-School.ttf")
SIZES = [56, 48, 42, 36, 30, 26, 22, 18, 16, 14, 12, 11]   # em sizes, largest first (11 px em = 8 px caps)
HEB_CPS = list(range(0x05D0, 0x05EB)) + [0x05F3, 0x05F4]
CODEPOINTS = sorted(set(list(range(0x20, 0x7F)) + HEB_CPS + [0x00B0, 0x2013, 0x2026, 0x20AA]))
# Letters that sit on the baseline and reach the letter height (no ascender
# or descender): used to match heights and the baseline between the fonts.
BASE_LETTERS = [0x05D1, 0x05D4, 0x05DE, 0x05E1, 0x05E9, 0x05EA, 0x05D8, 0x05E8]
# High-School supplies only A-Z, a-z (unicase: lower case draws the capitals);
# everything else (space, digits, punctuation) stays DejaVu.
LAT_CPS = list(range(0x41, 0x5B)) + list(range(0x61, 0x7B))
OUT = os.path.join(HERE, "..", "src", "fonts", "StopFonts.h")

def short_runs(img, n):
    """Number of runs of lit pixels, along rows and along columns, that are
    shorter than n: strokes the threshold thinned below the stroke width."""
    w, h = img.size
    px = img.load()
    count = 0
    for lines in ([[px[x, y] for x in range(w)] for y in range(h)],
                  [[px[x, y] for y in range(h)] for x in range(w)]):
        for line in lines:
            run = 0
            for v in line + [0]:
                if v >= 128:
                    run += 1
                else:
                    if 0 < run < n:
                        count += 1
                    run = 0
    return count

def raster(font, cp, asc, dy=0, fit=False, stroke=0):
    """Rasterise one glyph with its baseline `asc` pixels below the line top;
    dy moves it down (negative = up).

    With fit (used for Heshbon) the glyph is drawn 4x oversampled at each
    quarter-pixel offset and averaged down, and the offset whose coverage is
    closest to pure on/off wins: a crude grid fit. Heshbon has no hints, and a
    1 px stem straddling two pixel columns otherwise turns into a 2 px blob or
    a gap under the 1 bpp threshold.

    With stroke (used for High-School, whose strokes all share one width) the
    each run of lit pixels
    thinner than the stroke costs as much as a fully grey pixel: the plain
    coverage score happily drew one stem of an H 2 px wide and the other 1 px."""
    ch = chr(cp)
    adv = font.getlength(ch)
    pad = 8 + int(font.size) // 2
    W = int(adv) + 2 * pad + 4
    H = asc + 2 * pad + int(font.size)
    if fit:
        ss = 4
        big = ImageFont.truetype(font.path, font.size * ss)
        best = None
        for fy in (0, -1, 1):
            for fx in range(ss):
                hi = Image.new("L", (W * ss, H * ss), 0)
                ImageDraw.Draw(hi).text((pad * ss + fx, (pad + asc) * ss + fy), ch, font=big, fill=255, anchor="ls")
                lo = hi.reduce(ss)
                fuzz = sum(n * min(v, 255 - v) for v, n in enumerate(lo.histogram()))
                if stroke:
                    fuzz += 255 * short_runs(lo, round(stroke))
                if best is None or fuzz < best[0]:
                    best = (fuzz, lo)
        img = best[1]
    else:
        img = Image.new("L", (W, H), 0)
        ImageDraw.Draw(img).text((pad, pad + asc), ch, font=font, fill=255, anchor="ls")
    bw = img.point(lambda v: 255 if v >= 128 else 0)
    box = bw.getbbox()
    if box is None:                     # blank glyph (space)
        return dict(cp=cp, w=0, h=0, xo=0, yo=0, adv=round(adv), rows=[])
    x0, y0, x1, y1 = box
    rows = []
    for y in range(y0, y1):
        row = []
        for x in range(x0, x1):
            row.append(1 if bw.getpixel((x, y)) else 0)
        rows.append(row)
    return dict(cp=cp, w=x1 - x0, h=y1 - y0, xo=x0 - pad, yo=y0 - pad + dy, adv=round(adv), rows=rows)

REDRAWN = {ord(c) for c in "KRVXkrvx"}

def redrawn(cp, H, P):
    """K, R, V or X drawn to match High-School's H (size, stroke, waist)
    and, for R, its P (the bowl). Strokes are drawn 8x oversampled with the H
    stem's width and thresholded at half coverage."""
    w, h, rows = H["w"], H["h"], H["rows"]
    s = next((i for i, v in enumerate(rows[0]) if not v), w)            # stem width
    bar = [y for y, r in enumerate(rows) if all(r)]                       # H's crossbar rows
    waist = (bar[0] + bar[-1] + 1) / 2 if bar else h / 2
    ss = 8
    img = Image.new("L", (w * ss, h * ss), 0)
    d = ImageDraw.Draw(img)
    S, W, Hh, Y = s * ss, w * ss, h * ss, waist * ss
    def line(x0, y0, x1, y1):
        d.line([(x0, y0), (x1, y1)], fill=255, width=S if s <= 2 else round(S * 1.15))   # thicker only where it stays crisp
    c = chr(cp).upper()
    if c == "K":
        d.rectangle([0, 0, S - 1, Hh - 1], fill=255)
        line(S * 0.6, Y, W - S * 0.55, S * 0.3)
        line(S * 1.2, Y - S * 0.2, W - S * 0.55, Hh - S * 0.3)
    elif c == "X":
        line(S * 0.55, S * 0.3, W - S * 0.55, Hh - S * 0.3)
        line(W - S * 0.55, S * 0.3, S * 0.55, Hh - S * 0.3)
    elif c == "V":
        line(S * 0.55, S * 0.3, W / 2, Hh - S * 0.45)
        line(W - S * 0.55, S * 0.3, W / 2, Hh - S * 0.45)
    elif c == "R":
        pw, prow = P["w"], P["rows"]
        pbar = [y for y, r in enumerate(prow) if y > 0 and sum(r) >= pw - 1]   # the bowl's lower bar
        bowl_end = (pbar[-1] + 1) if pbar else round(waist)
        for y in range(min(bowl_end, h)):
            for x in range(min(pw, w)):
                if prow[y][x]:
                    d.rectangle([x * ss, y * ss, x * ss + ss - 1, y * ss + ss - 1], fill=255)
        d.rectangle([0, 0, S - 1, Hh - 1], fill=255)
        line(W * 0.45, bowl_end * ss - S * 0.5, W - S * 0.55, Hh - S * 0.3)
    lo = img.reduce(ss).point(lambda v: 255 if v >= 128 else 0)
    out = [[1 if lo.getpixel((x, y)) else 0 for x in range(w)] for y in range(h)]
    return dict(cp=cp, w=w, h=h, xo=H["xo"], yo=H["yo"], adv=H["adv"], rows=out)

def pack(rows, w):
    out = []
    stride = (w + 7) // 8
    for r in rows:
        bits = r + [0] * (stride * 8 - w)
        for b in range(stride):
            byte = 0
            for k in range(8):
                byte = (byte << 1) | bits[b * 8 + k]
            out.append(byte)
    return out

def has_glyph(font, cp):
    """True if the font maps cp (its bitmap differs from the .notdef box that a
    private-use code point gets)."""
    def mask(c):
        m = font.getmask(chr(c))
        return m.size, bytes(m)
    return mask(cp) != mask(0xE000)

def matched_font(path, letters, sz, asc, cap_h):
    """The font at `path` at the size (quarter-pixel steps) whose base letters
    are as tall as DejaVu's capitals at em size sz, and the vertical shift that
    puts their bottoms on the baseline."""
    best = None
    for q in range(int(sz * 4 * 1.0), int(sz * 4 * 1.45) + 1):
        f = ImageFont.truetype(path, q / 4)
        gs = [raster(f, cp, asc) for cp in letters]
        h = statistics.median(g["h"] for g in gs)
        err = abs(h - cap_h)
        if best is None or err < best[0]:
            dy = -round(statistics.median(g["yo"] + g["h"] - asc for g in gs))
            best = (err, f, dy)
    return best[1], best[2]

lines = ["// Generated by tools/gen_font.py: Latin letters from High-School (GeronimoFonts), Hebrew letters from Heshbon (AlefAlefAlef),",
         "// digits, punctuation and symbols from DejaVu Sans Bold. Do not edit.",
         "// Glyphs: ASCII 0x20-0x7E, Hebrew 0x05D0-0x05EA, geresh/gershayim, degree, en dash, ellipsis, shekel.",
         "#pragma once", "#include \"../Font.h\"", ""]
fontrefs = []
total = 0
heb_missing = None
lat_missing = None
for sz in SIZES:
    font = ImageFont.truetype(TTF, sz)
    asc, desc = font.getmetrics()
    cap_h = raster(font, ord("H"), asc)["h"]
    hfont, hdy = matched_font(HEB_FONT, BASE_LETTERS, sz, asc, cap_h)
    # High-School is drawn on a brick grid: capitals 640 of 1024 units tall,
    # sitting on the baseline. Scale it so they are exactly cap_h pixels.
    lfont, ldy = ImageFont.truetype(LAT_FONT, cap_h * 1024 / 640), 0
    if heb_missing is None:
        heb_missing = [cp for cp in HEB_CPS if not has_glyph(hfont, cp)]
        if heb_missing:
            print("Heshbon lacks", " ".join(f"U+{cp:04X}" for cp in heb_missing), "- using DejaVu for those")
        lat_missing = [cp for cp in LAT_CPS if not has_glyph(lfont, cp)]
        if lat_missing:
            print("High-School lacks", " ".join(f"U+{cp:04X}" for cp in lat_missing), "- using DejaVu for those")
    glyphs, bitmap = [], []
    def hs(c):   # a High-School letter as the loop below draws it
        g = raster(lfont, ord(c), asc, ldy, fit=True, stroke=cap_h / 5)
        g["xo"] = 0
        g["adv"] = g["w"] + max(1, round(lfont.size * 64 / 1024))
        return g
    ref_H, ref_P = hs("H"), hs("P")
    for cp in CODEPOINTS:
        if cp in HEB_CPS and cp not in heb_missing:
            g = raster(hfont, cp, asc, hdy, fit=True)
            # keep at least one blank column between neighbouring letters
            g["adv"] = max(g["adv"], g["xo"] + g["w"] + 1)
        elif cp in LAT_CPS and cp not in lat_missing:
            g = raster(lfont, cp, asc, ldy, fit=True, stroke=cap_h / 5)
            # every High-School letter starts at the pen and is followed by a
            # 64/1024 em gap; re-derive both from the fitted bitmap so the
            # quarter-pixel shifts do not make the spacing uneven
            g["xo"] = 0
            g["adv"] = g["w"] + max(1, round(lfont.size * 64 / 1024))
            if cp in REDRAWN:
                g = redrawn(cp, ref_H, ref_P)
        else:
            g = raster(font, cp, asc)
        g["off"] = len(bitmap)
        bitmap += pack(g["rows"], g["w"])
        glyphs.append(g)
    total += len(bitmap)
    name = f"font{sz}"
    lines.append(f"static const uint8_t {name}_bitmap[] PROGMEM = {{")
    for i in range(0, len(bitmap), 24):
        lines.append("  " + ",".join(f"0x{b:02X}" for b in bitmap[i:i+24]) + ",")
    lines.append("};")
    lines.append(f"static const Glyph {name}_glyphs[] PROGMEM = {{")
    for g in glyphs:
        lines.append(f"  {{0x{g['cp']:04X},{g['w']},{g['h']},{g['xo']},{g['yo']},{g['adv']},{g['off']}}},")
    lines.append("};")
    lines.append(f"static const Font {name} = {{{sz},{asc},{desc},{asc+desc},{len(glyphs)},{name}_glyphs,{name}_bitmap}};")
    lines.append("")
    fontrefs.append(name)
    print(f"size {sz}: ascent {asc} descent {desc} lineHeight {asc+desc}, caps {cap_h} px, "
          f"High-School {lfont.size:g} px shifted {ldy:+d}, Heshbon {hfont.size:g} px shifted {hdy:+d}, {len(glyphs)} glyphs, {len(bitmap)} bytes")
lines.append(f"static const Font* const STOP_FONTS[] = {{{', '.join('&' + n for n in fontrefs)}}};")
lines.append(f"static const int STOP_FONT_COUNT = {len(fontrefs)};")
open(OUT, "w").write("\n".join(lines) + "\n")
print(f"wrote {OUT}: {total} bitmap bytes")
