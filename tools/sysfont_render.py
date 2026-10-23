#!/usr/bin/env python3
"""Stand-in for tools/stopcli when it cannot run (Windows without WSL), used by
tools/sign_editor.py: the same RLE preview and text fit check, but the text is
drawn in a system font (Arial Bold) instead of the sign's own bitmap fonts, so
text sizes and line breaks are close, not exact. Circles, colours, unlit
rings, the figures (read from src/sprites/Sprites.h) and the beat graphics in
their start position (the page animates them) match the firmware.

Needs Pillow. Same interface as sign_editor.Renderer: sizes(), sprites(),
check(light, text, size), safe_run(["rle", "key=value", ...]).
"""
import math, os, re, struct

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTS = [r"C:\Windows\Fonts\arialbd.ttf", r"C:\Windows\Fonts\segoeuib.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"]
D, R_INNER, RING, MAX_LINES = 128, 61, 3, 8
DEF_BG = [48, 52, 12]
LEVEL = [0, 140, 195, 255]   # as Preview.cpp; black shows as 12,12,12


def _sizes():
    m = re.search(r"^SIZES = \[([^\]]*)\]", open(os.path.join(ROOT, "tools", "gen_font.py"), encoding="utf-8").read(), re.M)
    return [int(v) for v in m.group(1).split(",")]


def _sprites():
    src = open(os.path.join(ROOT, "src", "sprites", "Sprites.h"), encoding="utf-8").read()
    data = {m.group(1): bytes(int(v, 0) for v in re.findall(r"0x[0-9a-fA-F]+|\d+", m.group(2)))
            for m in re.finditer(r"static const uint8_t (\w+)\[\] PROGMEM = \{(.*?)\};", src, re.S)}
    out = []
    for m in re.finditer(r'static const Sprite \w+ = \{"([^"]*)", "([^"]*)", (\d+), (\d+), (\d+), (\d+), (\w+)\};', src):
        sid, name, w, h, n, ms, arr = m.groups()
        out.append({"id": sid, "name": name, "frames": int(n), "ms": int(ms), "w": int(w), "h": int(h), "data": data.get(arr, b"")})
    return out


def _is_heb(ch):
    return "\u0590" <= ch <= "\u05ff"


def _visual(line):
    """Rough bidi: Hebrew words read right to left, a line that starts in Hebrew runs right to left."""
    words = [w[::-1] if any(_is_heb(c) for c in w) else w for w in line.split(" ")]
    first = next((c for c in line if c.isalpha()), "")
    return " ".join(reversed(words)) if _is_heb(first) else " ".join(words)


class SysFontRenderer:
    def __init__(self):
        self.path = next((p for p in FONTS if os.path.exists(p)), None)
        self.error = "" if self.path else "no system font found (Arial Bold)"
        self._sizes, self._sprites, self._fonts = _sizes(), _sprites(), {}

    def describe(self):
        return f"system font {os.path.basename(self.path or '?')} (approximate text; install WSL or put tools/stopcli.exe for the exact one)"

    def sizes(self):
        return self._sizes

    def sprites(self):
        return [{k: s[k] for k in ("id", "name", "frames", "ms")} for s in self._sprites]

    def _font(self, size):
        if size not in self._fonts:
            self._fonts[size] = ImageFont.truetype(self.path, size, layout_engine=ImageFont.Layout.BASIC)   # no bidi of its own anywhere: _visual() does it
        return self._fonts[size]

    def _layout(self, text, cap=0):
        """-> (size, [(visual line, width)], line height) for the largest size that fits, or None."""
        paras = [p.split() for p in text.strip().split("\n")]
        for size in self._sizes:
            if cap and size > cap:
                continue
            f = self._font(size)
            asc, desc = f.getmetrics()
            lh = asc + desc
            lines = []
            for words in paras:   # greedy wrap at the circle's widest chord, then check every line's own chord
                cur = ""
                for w in words:
                    t = (cur + " " + w).strip()
                    if cur and f.getlength(t) > 2 * R_INNER:
                        lines.append(cur)
                        cur = w
                    else:
                        cur = t
                lines.append(cur)
            n = len(lines)
            if n > MAX_LINES or n * lh > 2 * R_INNER:
                continue
            top, ok, out = -n * lh / 2, True, []
            for k, line in enumerate(lines):
                y0, y1 = top + k * lh, top + (k + 1) * lh
                dy = max(abs(y0), abs(y1))
                chord = 2 * math.sqrt(max(0, R_INNER * R_INNER - dy * dy))
                wpx = f.getlength(line)
                if wpx > chord:
                    ok = False
                    break
                out.append((_visual(line), wpx))
            if ok:
                return size, out, lh
        return None

    def check(self, light, text, size=0):
        if not self.path:
            return {"ok": False, "error": self.error}
        L = self._layout(text, size)
        A = self._layout(text, 0) if size else L
        maxfit = A[0] if A else 0
        if not L:
            return {"ok": False, "error": "does not fit", "maxFit": maxfit}
        return {"ok": True, "size": L[0], "lines": len(L[1]), "maxFit": maxfit}

    def safe_run(self, args):
        try:
            return self.run(args)
        except Exception as e:
            self.error = str(e)
            return b""

    def run(self, args):
        if args[0] != "rle":
            return b""
        q = dict(a.split("=", 1) for a in args[1:] if "=" in a)
        mask = int(q.get("m", 7) or 7) & 7
        off_dark = q.get("o") == "1"
        k = int(q.get("k", 0) or 0)
        px = [[0] * D for _ in range(3 * D)]
        sprites = {s["id"]: s for s in self._sprites}
        for i in range(3):
            n = str(i)
            lit = (mask >> i) & 1
            bg = int(q["c" + n]) & 63 if q.get("c" + n, "") != "" else DEF_BG[i]
            fg = int(q.get("f" + n, 0) or 0) & 63
            ink = fg if fg != bg else (63 if bg == 0 else 0)
            g = q.get("g" + n, "")
            quads = [bg] * 4
            dia = D
            if g == "fx:quarters":
                qs = [int(v) & 63 for v in (q.get("q" + n) or "60,12,3").split(",")[:3]]
                quads = [bg] + qs
            elif g == "fx:pulse":
                dia = 96
            ring = ((1 if bg & 0x30 else 0) << 4) | ((1 if bg & 0x0C else 0) << 2) | (1 if bg & 3 else 0)
            y0 = i * D
            for y in range(D):
                dy = 2 * y - (D - 1)
                row = px[y0 + y]
                for x in range(D):
                    dx = 2 * x - (D - 1)
                    d2 = dx * dx + dy * dy
                    if lit:
                        if d2 <= dia * dia:
                            row[x] = quads[(0 if dx < 0 else 1) if dy < 0 else (3 if dx < 0 else 2)]
                    elif not off_dark and (D - 2 * RING) ** 2 <= d2 <= D * D:
                        row[x] = ring
            if not lit or g.startswith("fx:") or g.startswith("u:") or q.get("k" + n) == "off":
                continue
            s = sprites.get(g)
            if s:
                fr = 0 if s["frames"] <= 1 else (k // max(1, s["ms"])) % s["frames"]
                stride = (s["w"] + 7) // 8
                sx0, sy0 = D // 2 - s["w"] // 2, D // 2 - s["h"] // 2
                base = fr * stride * s["h"]
                for y in range(s["h"]):
                    dy = 2 * (sy0 + y) - (D - 1)
                    for x in range(s["w"]):
                        dx = 2 * (sx0 + x) - (D - 1)
                        b = s["data"][base + y * stride + (x >> 3)]
                        if (b >> (7 - (x & 7))) & 1 and dx * dx + dy * dy <= D * D:
                            px[y0 + sy0 + y][sx0 + x] = ink
                continue
            text = q.get("t" + n, "")
            L = self._layout(text, int(q.get("s" + n, 0) or 0)) if text.strip() and self.path else None
            if L:
                size, lines, lh = L
                img = Image.new("1", (D, D), 0)
                dr = ImageDraw.Draw(img)
                f = self._font(size)
                top = D / 2 - len(lines) * lh / 2
                for kk, (line, wpx) in enumerate(lines):
                    dr.text((D / 2 - wpx / 2, top + kk * lh), line, font=f, fill=1)
                data = img.load()
                for y in range(D):
                    for x in range(D):
                        if data[x, y]:
                            px[y0 + y][x] = ink
        pal, out = [], bytearray()
        flat = [v for row in px for v in row]
        for v in flat:
            if v not in pal:
                pal.append(v)
        pal = pal[:16]
        idx = {v: n for n, v in enumerate(pal)}
        hdr = bytearray(b"RL" + struct.pack("<HHB", D, 3 * D, len(pal)))
        for v in pal:
            hdr += bytes([12, 12, 12]) if v == 0 else bytes([LEVEL[(v >> 4) & 3], LEVEL[(v >> 2) & 3], LEVEL[v & 3]])
        cur, run = idx.get(flat[0], 0), 0
        for v in flat:
            c = idx.get(v, 0)
            if c != cur or run == 4095:
                out += struct.pack("<H", (cur << 12) | run)
                cur, run = c, 0
            run += 1
        out += struct.pack("<H", (cur << 12) | run)
        return bytes(hdr + out)
