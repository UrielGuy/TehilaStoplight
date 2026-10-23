#!/usr/bin/env python3
"""The sign's text engine and face renderer in plain Python: a port of
tools/stopcli (src/TextLayout.cpp, src/Face.cpp, src/Preview.cpp) that gives
the same bytes, so the offline editor needs no C++ and no WSL.

The bitmap fonts and figures are read from the generated headers
(src/fonts/StopFonts.h, src/sprites/Sprites.h) at start-up, so a regenerated
font or sprite sheet shows up here without touching this file. Keep the
logic in step with the C++; tools/render_parity_test.py compares the two
byte for byte.

Needs numpy. Command line, as stopcli (for diffing against it):
    stoplight_render.py sizes | sprites
    stoplight_render.py check <light> <text> [maxSize]
    stoplight_render.py rle|preview|ppm key=value ...
As a module: Renderer() with describe(), error, sizes(), sprites(),
check(light, text, size), safe_run(["rle"|"preview"|"ppm", "key=value", ...]),
the interface tools/sign_editor.py uses.
"""
import math, os, re, struct, sys, threading
from collections import OrderedDict

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTS_H = os.path.join(ROOT, "src", "fonts", "StopFonts.h")
SPRITES_H = os.path.join(ROOT, "src", "sprites", "Sprites.h")

# Face.h
LIGHTS, DIAMETER, MARGIN = 3, 128, 3
R_INNER = DIAMETER // 2 - MARGIN   # 61
MAX_LINES, RING = 8, 3
FX_NONE, FX_QUARTERS, FX_PULSE = 0, 1, 2
W, H = 128, 384
LEVEL = (0, 140, 195, 255)   # Preview.cpp: level 1 shown brighter than 1/3
MAX_ENUM_WORDS = 14


# ================================================================== C helpers
def _strtol(b):
    """glibc strtol(s, 0, 10) on bytes: leading space, sign, digits; saturates at the 64-bit range."""
    i, n = 0, len(b)
    while i < n and b[i] in b" \t\n\v\f\r":
        i += 1
    neg = False
    if i < n and b[i] in b"+-":
        neg = b[i] == 0x2D
        i += 1
    v = 0
    while i < n and 0x30 <= b[i] <= 0x39:
        v = v * 10 + b[i] - 0x30
        i += 1
    v = -v if neg else v
    return max(-(1 << 63), min((1 << 63) - 1, v))


def _wrap(v, bits, signed):
    v &= (1 << bits) - 1
    return v - (1 << bits) if signed and v >> (bits - 1) else v


def atoi(b):
    return _wrap(_strtol(b), 32, True)


def atol(b):
    return _strtol(b)


def cdiv(a, b):
    """C integer division (truncates toward zero)."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def cmod(a, b):
    return a - cdiv(a, b) * b


def f32(x):
    return struct.unpack("f", struct.pack("f", x))[0]


def _b(s):
    return s if isinstance(s, bytes) else str(s).encode("utf-8", "surrogatepass")


# ================================================================== Canvas.h
def rgb2(r, g, b):
    return ((r & 3) << 4) | ((g & 3) << 2) | (b & 3)


RGB2_BLACK, RGB2_RED, RGB2_ORANGE, RGB2_GREEN = 0, rgb2(3, 0, 0), rgb2(3, 1, 0), rgb2(0, 3, 0)


def rgb2_dim(c):
    return rgb2(1 if (c >> 4) & 3 else 0, 1 if (c >> 2) & 3 else 0, 1 if c & 3 else 0)


def rgb2_hue(c, deg):
    r, g, b = (c >> 4) & 3, (c >> 2) & 3, c & 3
    mx, mn = max(r, g, b), min(r, g, b)
    d = mx - mn
    if not d or cmod(deg, 360) == 0:
        return c
    if mx == r:
        h = cdiv(60 * (g - b), d) if g >= b else 360 - cdiv(60 * (b - g), d)
    elif mx == g:
        h = 120 + cdiv(60 * (b - r), d)
    else:
        h = 240 + cdiv(60 * (r - g), d)
    h = cmod(cmod(h + deg, 360) + 360, 360)
    up = mn + cdiv((h % 60) * d + 30, 60)
    down = mn + cdiv((60 - h % 60) * d + 30, 60)
    return (rgb2(mx, up, mn), rgb2(down, mx, mn), rgb2(mn, mx, up), rgb2(mn, down, mx), rgb2(up, mn, mx), rgb2(mx, mn, down))[min(h // 60, 5)]


def default_colour(light):
    return RGB2_RED if light == 0 else RGB2_ORANGE if light == 1 else RGB2_GREEN


# ================================================================== header data
class Font:
    __slots__ = ("size", "ascent", "descent", "lineHeight", "glyphs", "bitmap", "_bits")

    def glyph_bits(self, cp):
        """Bool array (h, w) of a glyph's bitmap."""
        a = self._bits.get(cp)
        if a is None:
            w, h, _, _, _, off = self.glyphs[cp]
            stride = (w + 7) // 8
            raw = np.frombuffer(self.bitmap, np.uint8, stride * h, off).reshape(h, stride)
            a = np.unpackbits(raw, axis=1)[:, :w].astype(bool)
            self._bits[cp] = a
        return a


class Sprite:
    __slots__ = ("id", "name", "w", "h", "nFrames", "frameMs", "data", "_masks")

    def frame_mask(self, frame):
        """Bool (h, w): pixels of this frame inside the full circle (Face::drawLight)."""
        m = self._masks.get(frame)
        if m is None:
            stride = (self.w + 7) // 8
            raw = np.frombuffer(self.data, np.uint8, stride * self.h, frame * stride * self.h).reshape(self.h, stride)
            m = np.unpackbits(raw, axis=1)[:, :self.w].astype(bool)
            sx0, sy0 = DIAMETER // 2 - self.w // 2, DIAMETER // 2 - self.h // 2
            m &= _D2[sy0:sy0 + self.h, sx0:sx0 + self.w] <= DIAMETER * DIAMETER
            self._masks[frame] = m
        return m


_C_ESC = {"n": "\n", "t": "\t", "\\": "\\", '"': '"', "'": "'", "r": "\r", "0": "\0"}


def _c_string(s):
    return re.sub(r"\\(.)", lambda m: _C_ESC.get(m.group(1), m.group(1)), s).encode("utf-8")


def _arrays(src):
    out = {}
    for m in re.finditer(r"static const uint8_t (\w+)\[\] PROGMEM = \{(.*?)\};", src, re.S):
        body = m.group(2)
        hexes = re.findall(r"0[xX]([0-9A-Fa-f]{2})\b", body)
        toks = [t for t in re.split(r"[\s,]+", body) if t]
        if len(hexes) == len(toks):
            out[m.group(1)] = bytes.fromhex("".join(hexes))
        else:
            out[m.group(1)] = bytes(int(t, 0) & 0xFF for t in toks)
    return out


def _num(t):
    return int(t, 0)


def load_fonts(path=FONTS_H):
    with open(path, encoding="utf-8") as f:
        src = f.read()
    bitmaps = _arrays(src)
    tables = {}
    for m in re.finditer(r"static const Glyph (\w+)\[\] PROGMEM = \{(.*?)\n\};", src, re.S):
        g = {}
        for e in re.finditer(r"\{\s*([^{}]*?)\s*\}", m.group(2)):
            cp, w, h, xo, yo, adv, off = [_num(t.strip()) for t in e.group(1).split(",")]
            g[cp & 0xFFFF] = (w & 0xFF, h & 0xFF, _wrap(xo, 8, True), _wrap(yo, 8, True), adv & 0xFF, off & 0xFFFFFFFF)
        tables[m.group(1)] = g
    defs = {}
    for m in re.finditer(r"static const Font (\w+) = \{([^{}]*)\};", src):
        v = [t.strip() for t in m.group(2).split(",")]
        f = Font()
        f.size, f.ascent, f.descent, f.lineHeight = [_num(t) & 0xFF for t in v[:4]]
        f.glyphs, f.bitmap, f._bits = tables[v[5]], bitmaps[v[6]], {}
        defs[m.group(1)] = f
    order = re.search(r"STOP_FONTS\[\]\s*=\s*\{([^}]*)\}", src).group(1)
    return [defs[n.strip().lstrip("&")] for n in order.split(",") if n.strip()]


def load_sprites(path=SPRITES_H):
    with open(path, encoding="utf-8") as f:
        src = f.read()
    data = _arrays(src)
    defs = {}
    for m in re.finditer(r'static const Sprite (\w+) = \{"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)",\s*(\w+),\s*(\w+),\s*(\w+),\s*(\w+),\s*(\w+)\};', src):
        s = Sprite()
        s.id, s.name = _c_string(m.group(2)), _c_string(m.group(3))
        s.w, s.h = _num(m.group(4)) & 0xFFFF, _num(m.group(5)) & 0xFFFF
        s.nFrames, s.frameMs = _num(m.group(6)) & 0xFF, _num(m.group(7)) & 0xFFFF
        s.data, s._masks = data[m.group(8)], {}
        defs[m.group(1)] = s
    order = re.search(r"SPRITES\[\]\s*=\s*\{([^}]*)\}", src).group(1)
    return [defs[n.strip().lstrip("&")] for n in order.split(",") if n.strip()]


# ================================================================== TextLayout.cpp
def utf8_decode(s):
    out, i, n = [], 0, len(s)
    while i < n:
        c = s[i]
        if c < 0x80:
            cp, k = c, 1
        elif c < 0xE0:
            cp, k = c & 0x1F, 2
        elif c < 0xF0:
            cp, k = c & 0x0F, 3
        else:
            cp, k = c & 0x07, 4
        j = 1
        while j < k and i + j < n:
            cp = ((cp << 6) | (s[i + j] & 0x3F)) & 0xFFFFFFFF
            j += 1
        out.append(cp)
        i += k
    return out


def utf8_encode(cp):
    if cp < 0x80:
        return bytes([cp])
    if cp < 0x800:
        return bytes([0xC0 | (cp >> 6), 0x80 | (cp & 0x3F)])
    if cp < 0x10000:
        return bytes([0xE0 | (cp >> 12), 0x80 | ((cp >> 6) & 0x3F), 0x80 | (cp & 0x3F)])
    return bytes([(0xF0 | (cp >> 18)) & 0xFF, 0x80 | ((cp >> 12) & 0x3F), 0x80 | ((cp >> 6) & 0x3F), 0x80 | (cp & 0x3F)])


def _is_hebrew(cp):
    return 0x0590 <= cp <= 0x05FF


def _is_nikud(cp):
    return 0x0591 <= cp <= 0x05C7


def _is_strong_l(cp):
    return 65 <= cp <= 90 or 97 <= cp <= 122 or 48 <= cp <= 57 or 0x00C0 <= cp <= 0x024F


_MIRROR = {40: 41, 41: 40, 91: 93, 93: 91, 123: 125, 125: 123, 60: 62, 62: 60}


def bidi_visual(cps):
    n = len(cps)
    if n == 0 or not any(0x0590 <= cp <= 0x05FF for cp in cps):
        return list(cps)
    cls = ["R" if _is_hebrew(cp) else "L" if _is_strong_l(cp) else "N" for cp in cps]
    for i in range(n):
        if cls[i] != "N":
            continue
        prev = nxt = "R"
        for k in range(i - 1, -1, -1):
            if cls[k] != "N":
                prev = cls[k]
                break
        for k in range(i + 1, n):
            if cls[k] != "N":
                nxt = cls[k]
                break
        cls[i] = prev if prev == nxt else "R"
    runs, i = [], 0
    while i < n:
        j = i
        while j < n and cls[j] == cls[i]:
            j += 1
        runs.append((i, j))
        i = j
    out = []
    for a, b in reversed(runs):
        if cls[a] == "R":
            out.extend(_MIRROR.get(cps[k], cps[k]) for k in range(b - 1, a - 1, -1))
        else:
            out.extend(cps[a:b])
    return out


def _measure(f, logical):
    """LineMetrics: (visual, penX, inkL, inkR, inkTop, inkBot, empty)."""
    visual = bidi_visual(logical)
    glyphs = f.glyphs
    pen, penx = 0, []
    empty, l0, r0, t0, b0 = True, 0, 0, 0, 0
    for cp in visual:
        g = glyphs.get(cp)
        penx.append(pen)
        if g is not None:
            if g[0]:
                l, r, t, b = pen + g[2], pen + g[2] + g[0], g[3], g[3] + g[1]
                if empty:
                    l0, r0, t0, b0, empty = l, r, t, b, False
                else:
                    if l < l0: l0 = l
                    if r > r0: r0 = r
                    if t < t0: t0 = t
                    if b > b0: b0 = b
            pen += g[4]
    return (visual, penx, l0, r0, t0, b0, empty)


def _chord_half(r, y0, y1):
    dy = max(abs(y0), abs(y1))
    if dy >= r:
        return 0
    return int(math.floor(math.sqrt(float(r * r) - float(dy * dy))))


class Layout:
    """TextLayout: font (None when it does not fit), lines, glyphs [(cp, x, y)] in
    tile coordinates (circle centre 64, 64), error."""
    __slots__ = ("font", "lines", "glyphs", "error", "_mask")

    def __init__(self):
        self.font, self.lines, self.glyphs, self.error, self._mask = None, 0, [], b"", None

    def ok(self):
        return self.font is not None and not self.error

    def mask(self):
        """Bool (128, 128) of the ink in its light's tile (drawLayoutRow, clipped to the tile)."""
        if self._mask is None:
            m = np.zeros((DIAMETER, DIAMETER), bool)
            for cp, x, y in self.glyphs:
                bits = self.font.glyph_bits(cp)
                h, w = bits.shape
                xa, xb, ya, yb = max(x, 0), min(x + w, DIAMETER), max(y, 0), min(y + h, DIAMETER)
                if xa < xb and ya < yb:
                    m[ya:yb, xa:xb] |= bits[ya - y:yb - y, xa - x:xb - x]
            self._mask = m
        return self._mask


class _Search:
    """bestForFont for one font and one word list (cached line metrics)."""

    def __init__(self, f, words, forced, cx, cy, r):
        self.f, self.words, self.forced, self.cx, self.cy, self.r = f, words, forced, cx, cy, r
        self.k = len(words)
        self.mets = {}

    def met(self, a, b):
        m = self.mets.get((a, b))
        if m is None:
            line = []
            for i in range(a, b):
                if i > a:
                    line.append(32)
                line.extend(self.words[i])
            m = self.mets[(a, b)] = _measure(self.f, line)
        return m

    def place(self, lines, build=False):
        """placeLines: worst fill (float32) or None; with build, (fill, glyphs)."""
        LH, cy, r = self.f.lineHeight, self.cy, self.r
        mets = [self.met(a, b) for a, b in lines]
        top_i, bot_i = 1 << 30, -(1 << 30)
        for i, m in enumerate(mets):
            if not m[6]:
                if i * LH + m[4] < top_i: top_i = i * LH + m[4]
                if i * LH + m[5] > bot_i: bot_i = i * LH + m[5]
        if top_i > bot_i:
            return None
        top = cy - cdiv(top_i + bot_i, 2)
        worst = 0.0
        glyphs = [] if build else None
        for i, m in enumerate(mets):
            if m[6]:
                continue
            line_top = top + i * LH
            half = _chord_half(r, line_top + m[4] - cy, line_top + m[5] - cy)
            ink_w = m[3] - m[2]
            if half <= 0 or ink_w > 2 * half:
                return None
            fill = f32(ink_w / (2.0 * half))
            if fill > worst:
                worst = fill
            if build:
                origin = self.cx - ink_w // 2 - m[2]
                for cp, px in zip(m[0], m[1]):
                    g = self.f.glyphs.get(cp)
                    if g is None or not g[0]:
                        continue
                    glyphs.append((cp, origin + px + g[2], line_top + g[3]))
        return (worst, glyphs) if build else worst

    def best(self, max_n):
        """-> list of (a, b) lines of the chosen layout, or None."""
        k, forced, f = self.k, self.forced, self.f
        min_lines = 1 + sum(1 for i in range(k - 1) if forced[i])
        if min_lines > max_n:
            return None
        if k <= MAX_ENUM_WORDS:
            return self._enumerate(max_n, min_lines)
        return self._greedy(max_n)

    def _enumerate(self, max_n, min_lines):
        # The C++ walks every break pattern and keeps the first one with the
        # fewest lines, then the smallest worst fill: the minimum of
        # (lines, fill, pattern). Same choice, found line count by line count.
        k, forced, f = self.k, self.forced, self.f
        adv = []
        for wd in self.words:
            a = 0
            for cp in wd:
                g = f.glyphs.get(cp)
                if g is not None:
                    a += g[4]
            adv.append(a)
        sp = f.glyphs.get(32)
        space = sp[4] if sp is not None else f.size // 3
        limit = 2 * self.r + 4
        two_r = 2 * self.r
        # line (a, b) passes the advance prune and the plain diameter bound
        ok = {}

        def line_ok(a, b):
            v = ok.get((a, b))
            if v is None:
                m = self.met(a, b)
                v = ok[(a, b)] = m[6] or (m[3] - m[2]) <= two_r
            return v

        for n in range(min_lines, max_n + 1):
            best = None
            stack = []

            def rec(a, left, pat):
                nonlocal best
                if left == 1:
                    for i in range(a, k - 1):
                        if forced[i]:
                            return
                    if sum(adv[a:k]) + space * (k - 1 - a) > limit or not line_ok(a, k):
                        return
                    stack.append((a, k))
                    fill = self.place(stack)
                    stack.pop()
                    if fill is not None and (best is None or (fill, pat) < best[:2]):
                        best = (fill, pat, list(stack) + [(a, k)])
                    return
                lw = -space
                for b in range(a + 1, k - left + 2):
                    if b > a + 1 and forced[b - 2]:
                        break
                    lw += adv[b - 1] + space
                    if lw > limit:
                        break
                    if not line_ok(a, b):
                        continue
                    stack.append((a, b))
                    rec(b, left - 1, pat | (1 << (b - 1)))
                    stack.pop()

            rec(0, n, 0)
            if best is not None:
                return best[2]
        return None

    def _greedy(self, max_n):
        k, forced, f, r, cy = self.k, self.forced, self.f, self.r, self.cy
        LH = f.lineHeight
        found, best_lines, best_fill, out = False, 0, 0.0, None
        for n in range(1, max_n + 1):
            block_top = cy - (n * LH) // 2
            lines, w, bad = [], 0, False
            while w < k:
                if len(lines) >= n:
                    bad = True
                    break
                li = len(lines)
                limit = 2 * _chord_half(r, block_top + li * LH + LH // 4 - cy, block_top + (li + 1) * LH - LH // 4 - cy)
                end = w + 1
                while end < k and not forced[end - 1]:
                    m = self.met(w, end + 1)
                    if (0 if m[6] else m[3] - m[2]) > limit:
                        break
                    end += 1
                lines.append((w, end))
                w = end
            if bad:
                continue
            fill = self.place(lines)
            if fill is None:
                continue
            nl = len(lines)
            if not found or nl < best_lines or (nl == best_lines and fill < best_fill):
                found, best_lines, best_fill, out = True, nl, fill, lines
        return out


def layout_in_circle(text, fonts, cx=64, cy=64, r_inner=R_INNER, max_lines=MAX_LINES, max_font_size=0):
    """layoutInCircle for UTF-8 bytes; glyph positions relative to (cx, cy)."""
    L = Layout()
    cps = []
    for cp in utf8_decode(text):
        if _is_nikud(cp) or cp in (0x200E, 0x200F, 0xFEFF):
            continue
        if cp == 9 or cp == 0xA0:
            cp = 32
        if cp == 13:
            continue
        if cp < 0x20 and cp != 10:
            continue
        cps.append(cp)
    words, forced, cur = [], [], []
    for cp in cps:
        if cp == 32 or cp == 10:
            if cur:
                words.append(tuple(cur))
                forced.append(False)
                cur = []
            if cp == 10 and words:
                forced[-1] = True
        else:
            cur.append(cp)
    if cur:
        words.append(tuple(cur))
        forced.append(False)
    if not words:
        L.error = b"empty"
        return L
    forced[-1] = False
    if not fonts:
        L.error = b"no fonts"
        return L
    g0 = fonts[0].glyphs
    for wd in words:
        for cp in wd:
            if cp not in g0:
                L.error = b"unsupported character: " + utf8_encode(cp)
                return L
    for f in fonts:
        if max_font_size > 0 and f.size > max_font_size:
            continue
        max_n = min((2 * r_inner) // f.lineHeight, max_lines)
        s = _Search(f, words, forced, cx, cy, r_inner)
        lines = s.best(max_n)
        if lines is not None:
            _, L.glyphs = s.place(lines, build=True)
            L.font, L.lines = f, len(lines)
            return L
    L.error = b"too long to fit in the circle"
    return L


# ================================================================== Face.cpp
class Beat:
    __slots__ = ("bpm", "gridMs", "frameMs")

    def __init__(self, bpm=0, grid_ms=0, frame_ms=0):
        self.bpm, self.gridMs, self.frameMs = bpm, grid_ms, frame_ms

    def steps(self):
        if not self.bpm:
            return 0
        start = self.gridMs - self.frameMs if self.gridMs >= self.frameMs else 0
        return (self.gridMs * self.bpm // 60000 - start * self.bpm // 60000) & 0xFFFFFFFF

    def phase(self):
        return self.gridMs * self.bpm % 60000 if self.bpm else 0


class LightSpec:
    __slots__ = ("text", "sprite", "bg", "fg", "effect", "quarters", "hueBg", "hueFg")

    def __init__(self):
        self.text, self.sprite, self.bg, self.fg, self.effect = None, None, RGB2_RED, RGB2_BLACK, FX_NONE
        self.quarters = [rgb2(3, 3, 0), RGB2_GREEN, rgb2(0, 0, 3)]
        self.hueBg = self.hueFg = 0


def _hue_deg(steps, per_beat):
    return cmod(steps * per_beat, 360)


def beat_bg(s, b):
    return rgb2_hue(s.bg, _hue_deg(b.steps(), s.hueBg)) if s.hueBg else s.bg


def beat_fg(s, b):
    return rgb2_hue(s.fg, _hue_deg(b.steps(), s.hueFg)) if s.hueFg else s.fg


def pulse_diameter(b):
    if not b.bpm:
        return DIAMETER * 3 // 4
    rem = 60000 - b.phase()
    return DIAMETER * 3 // 4 + ((DIAMETER // 4) * rem * rem + 1800000000) // 3600000000


def effect_by_id(gid):
    return FX_QUARTERS if gid == b"fx:quarters" else FX_PULSE if gid == b"fx:pulse" else FX_NONE


# doubled coordinates: (2x-127)^2 + (2y-127)^2
_ax = 2 * np.arange(DIAMETER, dtype=np.int32) - (DIAMETER - 1)
_D2 = _ax[None, :] ** 2 + _ax[:, None] ** 2
_QIDX = np.where(_ax[:, None] < 0, np.where(_ax[None, :] < 0, 0, 1), np.where(_ax[None, :] < 0, 3, 2)).astype(np.uint8)
_FULL = _D2 <= DIAMETER * DIAMETER
_RING = _FULL & (_D2 >= (DIAMETER - 2 * RING) ** 2)


def sprite_frame(s, elapsed_ms):
    return 0 if s.nFrames <= 1 else (elapsed_ms // (s.frameMs or 1)) % s.nFrames


def draw_light(tile, sp, lit, elapsed_ms, beat, off_dark):
    """Face::drawLight into a (128, 128) uint8 view."""
    base = beat_bg(sp, beat) if lit else sp.bg
    ink = beat_fg(sp, beat) if lit else sp.fg
    if ink == base:
        ink = rgb2(3, 3, 3) if base == RGB2_BLACK else RGB2_BLACK
    quarter = [base] * 4
    dia = DIAMETER
    if lit and sp.effect == FX_QUARTERS:
        n = beat.steps()
        deg = _hue_deg(n, sp.hueBg)
        cols = [base] + [rgb2_hue(q, deg) for q in sp.quarters]
        quarter = [cols[(q + 4 - n % 4) % 4] for q in range(4)]
    elif lit and sp.effect == FX_PULSE:
        dia = pulse_diameter(beat)
    if lit:
        inside = _FULL if dia == DIAMETER else (_D2 <= dia * dia)
        if quarter.count(quarter[0]) == 4:
            tile[...] = np.where(inside, np.uint8(quarter[0]), np.uint8(0))
        else:
            tile[...] = np.where(inside, np.array(quarter, np.uint8)[_QIDX], np.uint8(0))
    elif not off_dark:
        tile[...] = np.where(_RING, np.uint8(rgb2_dim(sp.bg)), np.uint8(0))
    else:
        tile[...] = 0
    s = sp.sprite if lit and not sp.effect else None
    if s is not None:
        sx0, sy0 = DIAMETER // 2 - s.w // 2, DIAMETER // 2 - s.h // 2
        m = s.frame_mask(sprite_frame(s, elapsed_ms))
        # rows/columns outside the tile are dropped, as drawLight's loops do
        ya, yb, xa, xb = max(sy0, 0), min(sy0 + s.h, DIAMETER), max(sx0, 0), min(sx0 + s.w, DIAMETER)
        if ya < yb and xa < xb:
            tile[ya:yb, xa:xb][m[ya - sy0:yb - sy0, xa - sx0:xb - sx0]] = ink
    elif lit and not sp.effect and sp.text is not None and sp.text.ok():
        tile[sp.text.mask()] = ink


# ================================================================== Preview.cpp
class Image:
    """BitmapCanvas after a full face draw: Rgb2 pixels plus the palette in
    order of first use (black first, at most 16; later colours draw black)."""

    def __init__(self, px):
        flat = px.ravel()
        u, first = np.unique(flat, return_index=True)
        pal = [0] + [int(c) for c in u[np.argsort(first)] if c != 0]
        self.pal = pal[:16]
        lut = np.zeros(64, np.uint8)
        for i, c in enumerate(self.pal):
            lut[c] = i
        self.idx = lut[px]

    def colours(self):
        return np.array(self.pal, np.uint8)[self.idx]

    @staticmethod
    def _rgb(c):
        return (12, 12, 12) if c == 0 else (LEVEL[(c >> 4) & 3], LEVEL[(c >> 2) & 3], LEVEL[c & 3])

    def rle(self):
        hdr = bytearray(b"RL" + struct.pack("<HHB", W, H, len(self.pal)))
        for c in self.pal:
            hdr += bytes(self._rgb(c))
        flat = self.idx.ravel()
        starts = np.concatenate(([0], np.flatnonzero(flat[1:] != flat[:-1]) + 1))
        lens = np.diff(np.append(starts, flat.size))
        vals = flat[starts].astype(np.uint16)
        pieces = (lens + 4094) // 4095
        if (pieces == 1).all():
            words = (vals << 12) | lens.astype(np.uint16)
        else:
            rv = np.repeat(vals, pieces)
            rl = np.full(rv.size, 4095, np.int64)
            rl[np.cumsum(pieces) - 1] = lens - 4095 * (pieces - 1)
            words = (rv << 12) | rl.astype(np.uint16)
        return bytes(hdr) + words.astype("<u2").tobytes()

    def bmp(self):
        row_bytes = W // 2
        hdr_len = 14 + 40 + 16 * 4
        hdr = bytearray(hdr_len)
        hdr[0:2] = b"BM"
        struct.pack_into("<I", hdr, 2, hdr_len + row_bytes * H)
        struct.pack_into("<I", hdr, 10, hdr_len)
        struct.pack_into("<IiiHH", hdr, 14, 40, W, H, 1, 4)
        struct.pack_into("<I", hdr, 34, row_bytes * H)
        struct.pack_into("<I", hdr, 46, 16)
        for i in range(16):
            c = self.pal[i] if i < len(self.pal) else 0
            r, g, b = self._rgb(c)
            hdr[54 + i * 4:58 + i * 4] = bytes((b, g, r, 0))
        packed = (self.idx[:, 0::2] << 4) | self.idx[:, 1::2]
        return bytes(hdr) + packed[::-1].astype(np.uint8).tobytes()

    def ppm(self):
        lv = np.array(LEVEL, np.uint8)
        c = self.colours()
        rgb = np.stack([lv[(c >> 4) & 3], lv[(c >> 2) & 3], lv[c & 3]], axis=-1)
        rgb[c == 0] = 12
        return b"P6\n%d %d\n255\n" % (W, H) + rgb.tobytes()


# ================================================================== engine
def _json_escape(b):
    o = bytearray()
    for c in b:
        if c in (0x22, 0x5C):
            o += bytes((0x5C, c))
        elif c == 10:
            o += b"\\n"
        elif c < 0x20:
            o += b"\\u%04x" % c
        else:
            o.append(c)
    return bytes(o)


class Engine:
    def __init__(self, fonts_h=FONTS_H, sprites_h=SPRITES_H):
        self.fonts = load_fonts(fonts_h)
        self.sprites = load_sprites(sprites_h)
        self._sprite_ids = {s.id: s for s in reversed(self.sprites)}   # first wins, as spriteById
        self._layouts = OrderedDict()
        self._lock = threading.Lock()

    def layout(self, text, max_size=0):
        """Face::layoutFor in tile coordinates (the same for every light). Cached."""
        key = (_b(text), max_size)
        with self._lock:
            L = self._layouts.get(key)
            if L is not None:
                self._layouts.move_to_end(key)
                return L
        L = layout_in_circle(key[0], self.fonts, max_font_size=max_size)
        with self._lock:
            self._layouts[key] = L
            while len(self._layouts) > 512:
                self._layouts.popitem(last=False)
        return L

    # ---- stopcli's commands, byte for byte
    def sizes_json(self):
        return b"[" + b",".join(b"%d" % f.size for f in self.fonts) + b"]\n"

    def sprites_json(self):
        return b"[" + b",".join(b'{"id":"%s","name":"%s","frames":%d,"ms":%d}' % (s.id, s.name, s.nFrames, s.frameMs)
                                for s in self.sprites) + b"]\n"

    def check_json(self, light, text, max_size=0):
        L = self.layout(text, max_size)
        A = self.layout(text, 0) if max_size else L
        max_fit = A.font.size if A.ok() else 0
        if L.ok():
            return b'{"ok":true,"size":%d,"lines":%d,"maxFit":%d}\n' % (L.font.size, L.lines, max_fit)
        return b'{"ok":false,"error":"%s","maxFit":%d}\n' % (_json_escape(L.error), max_fit)

    def render(self, args):
        """Face::draw for stopcli's key=value arguments (bytes or str) -> Image."""
        kv = []
        for a in args:
            a = _b(a)
            i = a.find(b"=")
            if i >= 0:
                kv.append((a[:i], a[i + 1:]))

        def get(key):
            for k, v in kv:
                if k == key:
                    return v
            return b""

        specs = []
        for i in range(LIGHTS):
            n = b"%d" % i
            sp = LightSpec()
            g = get(b"g" + n)
            sp.sprite = self._sprite_ids.get(g) if g else None
            sp.effect = effect_by_id(g)
            if sp.sprite is None and not sp.effect and get(b"k" + n) != b"off":
                sp.text = self.layout(get(b"t" + n), atoi(get(b"s" + n)))
            c = get(b"c" + n)
            sp.bg = (atoi(c) & 63) if c else default_colour(i)
            sp.fg = atoi(get(b"f" + n)) & 63
            sp.hueBg = _wrap(cmod(atoi(get(b"hc" + n)), 360), 16, True)
            sp.hueFg = _wrap(cmod(atoi(get(b"hf" + n)), 360), 16, True)
            q = get(b"q" + n)
            for k in range(3):
                if not q:
                    break
                sp.quarters[k] = atoi(q) & 63
                j = q.find(b",")
                if j < 0:
                    break
                q = q[j + 1:]
            specs.append(sp)
        k = atol(get(b"k")) & 0xFFFFFFFF
        b = get(b"b")
        beat = Beat(atoi(get(b"bpm")) & 0xFFFF, (atol(b) & 0xFFFFFFFF) if b else k, k)
        mask = (atoi(get(b"m")) & 7) if get(b"m") else 7
        off_dark = atoi(get(b"o")) != 0
        px = np.zeros((H, W), np.uint8)
        for light in range(LIGHTS):
            draw_light(px[light * DIAMETER:(light + 1) * DIAMETER], specs[light], (mask >> light) & 1, k, beat, off_dark)
        return Image(px)

    def run(self, args):
        """stopcli's command line -> its stdout bytes."""
        args = [_b(a) for a in args]
        if not args:
            raise ValueError("usage: check|preview|ppm ...")
        cmd = args[0]
        if cmd == b"sizes":
            return self.sizes_json()
        if cmd == b"sprites":
            return self.sprites_json()
        if cmd == b"check":
            if len(args) < 3:
                raise ValueError("check <light> <text> [maxSize]")
            return self.check_json(atoi(args[1]), args[2], atoi(args[3]) if len(args) > 3 else 0)
        if cmd in (b"rle", b"preview", b"ppm"):
            img = self.render(args[1:])
            return img.rle() if cmd == b"rle" else img.bmp() if cmd == b"preview" else img.ppm()
        raise ValueError("unknown command")


_ENGINE = None
_ENGINE_LOCK = threading.Lock()


def engine():
    global _ENGINE
    with _ENGINE_LOCK:
        if _ENGINE is None:
            _ENGINE = Engine()
        return _ENGINE


class Renderer:
    """The interface tools/sign_editor.py uses (as its stopcli Renderer)."""

    def __init__(self):
        self.error = ""
        try:
            self.E = engine()
        except Exception as e:
            self.E = None
            self.error = f"cannot read the fonts / figures from src/: {e}"

    def describe(self):
        return "built-in (Python port of the firmware's text engine)"

    def safe_run(self, args):
        try:
            return self.E.run(args)
        except Exception as e:
            self.error = str(e)
            return b""

    def _json(self, args, default):
        import json
        try:
            return json.loads(self.E.run(args).decode("utf-8", "replace")) or default
        except Exception as e:
            self.error = str(e)
            return default

    def sizes(self):
        return self._json(["sizes"], [])

    def sprites(self):
        return self._json(["sprites"], [])

    def check(self, light, text, size=0):
        return self._json(["check", light, text, size], {"ok": False, "error": "engine failed"})


def main(argv):
    try:
        out = Engine().run([os.fsencode(a) for a in argv[1:]])
    except ValueError as e:
        print(e, file=sys.stderr)
        return 2
    sys.stdout.buffer.write(out)
    sys.stdout.buffer.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
