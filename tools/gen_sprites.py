#!/usr/bin/env python3
"""Procedural pictogram figures -> src/sprites/Sprites.h (1 bpp frames).

Each sprite is a list of poses. A pose is a dict of joint angles (degrees,
0 = straight down, positive = clockwise as seen by the viewer) plus optional
torso lean and vertical offset. Limbs are thick lines; the head is a disc.
Frames are 64 x 96 px, which fits inside the 128 px circle with margin.
"""
import math, os
from PIL import Image, ImageDraw

W, H = 64, 96
OUT = os.path.join(os.path.dirname(__file__), "..", "src", "sprites", "Sprites.h")

def pt(x, y, ang, length):
    a = math.radians(ang)
    return (x + length * math.sin(a), y + length * math.cos(a))

BIG = 4            # render at 4x on a large canvas, then fit into W x H
CW, CH = 640, 640

def draw_figure(d, pose, hip):
    """One jointed figure. pose: lean (torso angle from vertical, + = viewer's
    right), head (extra head angle), la/lf ra/rf (upper arm / forearm, absolute
    angles, 0 = hanging down, negative = viewer's left), ll/ls rl/rs (thigh /
    shin)."""
    T = 7 * BIG
    lean = pose.get("lean", 0)
    neck = pt(hip[0], hip[1], 180 + lean, 30 * BIG)
    head_c = pt(neck[0], neck[1], 180 + lean + pose.get("head", 0), 11 * BIG)
    d.line([hip, neck], fill=255, width=T + 3 * BIG)
    r = 7.5 * BIG
    d.ellipse([head_c[0] - r, head_c[1] - r, head_c[0] + r, head_c[1] + r], fill=255)
    def limb(origin, a1, l1, a2, l2):
        j = pt(origin[0], origin[1], a1, l1 * BIG)
        e = pt(j[0], j[1], a2, l2 * BIG)
        d.line([origin, j], fill=255, width=T); d.line([j, e], fill=255, width=T)
        for q in (origin, j, e):
            d.ellipse([q[0] - T / 2, q[1] - T / 2, q[0] + T / 2, q[1] + T / 2], fill=255)
    sh = pt(neck[0], neck[1], lean, 4 * BIG)
    limb(sh, pose.get("la", -20), 17, pose.get("lf", -20), 15)
    limb(sh, pose.get("ra", 20), 17, pose.get("rf", 20), 15)
    limb(hip, pose.get("ll", -8), 20, pose.get("ls", -8), 19)
    limb(hip, pose.get("rl", 8), 20, pose.get("rs", 8), 19)

def render_big(pose):
    """A frame: the main figure (with optional dx/dy offsets), an optional
    second figure under "second" (its own pose dict, with dx/dy), and extra
    "shapes": ("circle", cx, cy, r, width) / ("disc", cx, cy, r) /
    ("line", x1, y1, x2, y2, width), in unscaled px relative to the main hip."""
    img = Image.new("L", (CW, CH), 0)
    d = ImageDraw.Draw(img)
    hip = (CW / 2 + pose.get("dx", 0) * BIG, CH * 0.6 + pose.get("dy", 0) * BIG)
    draw_figure(d, pose, hip)
    if "second" in pose:
        q = pose["second"]
        draw_figure(d, q, (CW / 2 + q.get("dx", 0) * BIG, CH * 0.6 + q.get("dy", 0) * BIG))
    ox, oy = CW / 2, CH * 0.6
    for sh in pose.get("shapes", []):
        if sh[0] == "circle":
            _, cx, cy, r, w = sh
            d.ellipse([ox + (cx - r) * BIG, oy + (cy - r) * BIG, ox + (cx + r) * BIG, oy + (cy + r) * BIG], outline=255, width=int(w * BIG))
        elif sh[0] == "disc":
            _, cx, cy, r = sh
            d.ellipse([ox + (cx - r) * BIG, oy + (cy - r) * BIG, ox + (cx + r) * BIG, oy + (cy + r) * BIG], fill=255)
        elif sh[0] == "line":
            _, x1, y1, x2, y2, w = sh
            d.line([(ox + x1 * BIG, oy + y1 * BIG), (ox + x2 * BIG, oy + y2 * BIG)], fill=255, width=int(w * BIG))
    return img

def fit_frames(bigs):
    """Crop all frames to their union bounding box, scale to fit W x H
    keeping aspect, centre. Thresholded after an anti-aliased downscale."""
    boxes = [b.getbbox() for b in bigs]
    x0 = min(b[0] for b in boxes); y0 = min(b[1] for b in boxes)
    x1 = max(b[2] for b in boxes); y1 = max(b[3] for b in boxes)
    bw, bh = x1 - x0, y1 - y0
    scale = min((W - 2) / bw, (H - 2) / bh)
    tw, th = max(1, round(bw * scale)), max(1, round(bh * scale))
    out = []
    for b in bigs:
        small = b.crop((x0, y0, x1, y1)).resize((tw, th), Image.LANCZOS)
        img = Image.new("1", (W, H), 0)
        img.paste(small.point(lambda v: 255 if v >= 110 else 0).convert("1"), ((W - tw) // 2, (H - th) // 2))
        out.append(img)
    return out

def render(poses):
    return fit_frames([render_big(p) for p in poses])

SPRITES = [
  ("standing", "Person standing", 1000, [
      dict(la=-28, lf=-14, ra=28, rf=14, ll=-14, ls=-12, rl=14, rs=12),
  ]),
  ("ready", "On your marks, get set", 700, [
      dict(la=-28, lf=-14, ra=28, rf=14, ll=-14, ls=-12, rl=14, rs=12),                   # standing
      dict(lean=35, head=-15, la=-60, lf=-30, ra=-40, rf=-10, ll=-45, ls=35, rl=25, rs=-10),   # crouch, hands reaching down
      dict(lean=72, head=-40, la=-95, lf=-5, ra=-85, rf=5, ll=-70, ls=20, rl=55, rs=75),    # set: hips up, hands on the line
  ]),
  ("breakdance", "Breakdancing", 200, [
      dict(lean=8,   la=-70,  lf=-20,  ra=60,   rf=20,   ll=-30, ls=10,  rl=30,  rs=-10),               # toprock, knees bent
      dict(lean=50,  head=-20, la=-20,  lf=10,   ra=-40,  rf=-10, ll=-45, ls=30,  rl=60,  rs=20),         # dropping, hands reaching the floor
      dict(lean=100, head=-30, la=10,   lf=0,    ra=-10,  rf=-5,  ll=-150, ls=-170, rl=-100, rs=-30),     # one-hand freeze, leg kicked up
      dict(lean=180, la=15,   lf=5,    ra=-15,  rf=-5,   ll=-155, ls=-135, rl=155, rs=135),              # headstand V
      dict(lean=180, la=20,   lf=10,   ra=-20,  rf=-10,  ll=-110, ls=-160, rl=175, rs=120),              # windmill, legs scissoring
      dict(lean=-180, la=-15, lf=-5,   ra=15,   rf=5,    ll=110,  ls=160,  rl=-175, rs=-120),             # windmill, other side
      dict(lean=-100, head=30, la=-10, lf=5,    ra=10,   rf=0,    ll=150,  ls=170,  rl=100,  rs=30),       # freeze on the other hand
      dict(lean=-45, head=20, la=40,   lf=10,   ra=20,   rf=-10,  ll=45,   ls=-30,  rl=-60,  rs=-20),      # coming back up
  ]),
  ("walking", "Walking", 220, [
      dict(lean=4, la=25,  lf=35,  ra=-25, rf=-15, ll=-30, ls=-20, rl=25,  rs=40),
      dict(lean=4, la=10,  lf=15,  ra=-10, rf=-5,  ll=-8,  ls=-4,  rl=8,   rs=30),
      dict(lean=4, la=-25, lf=-15, ra=25,  rf=35,  ll=25,  ls=40,  rl=-30, rs=-20),
      dict(lean=4, la=-10, lf=-5,  ra=10,  rf=15,  ll=8,   ls=30,  rl=-8,  rs=-4),
  ]),
  ("running", "Running", 150, [
      dict(lean=22, la=45,  lf=-30, ra=-60, rf=-150, ll=-50, ls=-15, rl=40,  rs=95),
      dict(lean=22, la=15,  lf=-60, ra=-30, rf=-120, ll=-15, ls=30,  rl=15,  rs=70),
      dict(lean=22, la=-60, lf=-150, ra=45, rf=-30,  ll=40,  ls=95,  rl=-50, rs=-15),
      dict(lean=22, la=-30, lf=-120, ra=15, rf=-60,  ll=15,  ls=70,  rl=-15, rs=30),
  ]),
  ("hug", "Two people hugging", 700, [
      dict(dx=-22, lean=4,  la=-30, lf=-20, ra=55,  rf=40,  ll=-8, ls=-8, rl=8, rs=8,
           second=dict(dx=22, lean=-4, la=-55, lf=-40, ra=30, rf=20, ll=-8, ls=-8, rl=8, rs=8)),
      dict(dx=-11, lean=10, la=-20, lf=-10, ra=75,  rf=100, ll=-8, ls=-8, rl=8, rs=8,
           second=dict(dx=11, lean=-10, la=-75, lf=-100, ra=20, rf=10, ll=-8, ls=-8, rl=8, rs=8)),
      dict(dx=-11, lean=12, head=-10, la=-20, lf=-10, ra=80, rf=110, ll=-8, ls=-8, rl=8, rs=8,
           second=dict(dx=11, lean=-12, head=10, la=-80, lf=-110, ra=20, rf=10, ll=-8, ls=-8, rl=8, rs=8)),
  ]),
  ("highfive", "High five", 350, [
      dict(dx=-20, la=-15, lf=-10, ra=100, rf=150, ll=-10, ls=-8, rl=10, rs=8,
           second=dict(dx=20, la=-100, lf=-150, ra=15, rf=10, ll=-10, ls=-8, rl=10, rs=8)),
      dict(dx=-18, la=-15, lf=-10, ra=130, rf=165, ll=-10, ls=-8, rl=10, rs=8,
           second=dict(dx=18, la=-130, lf=-165, ra=15, rf=10, ll=-10, ls=-8, rl=10, rs=8)),
      dict(dx=-16, lean=3, la=-15, lf=-10, ra=145, rf=175, ll=-10, ls=-8, rl=10, rs=8,
           second=dict(dx=16, lean=-3, la=-145, lf=-175, ra=15, rf=10, ll=-10, ls=-8, rl=10, rs=8)),
      dict(dx=-18, la=-15, lf=-10, ra=130, rf=165, ll=-10, ls=-8, rl=10, rs=8,
           second=dict(dx=18, la=-130, lf=-165, ra=15, rf=10, ll=-10, ls=-8, rl=10, rs=8)),
  ]),
  ("bow", "Taking a bow", 500, [
      dict(la=-15, lf=-10, ra=15, rf=10, ll=-10, ls=-8, rl=10, rs=8),
      dict(lean=40, head=-10, la=-20, lf=-10, ra=0, rf=-10, ll=-10, ls=-8, rl=10, rs=8),
      dict(lean=80, head=-20, la=-30, lf=-10, ra=-10, rf=-5, ll=-10, ls=-8, rl=10, rs=8),
      dict(lean=40, head=-10, la=-20, lf=-10, ra=0, rf=-10, ll=-10, ls=-8, rl=10, rs=8),
  ]),
  ("dj", "DJ at the decks", 260, [
      dict(dy=-6, head=-12, la=-130, lf=-60, ra=35, rf=70, ll=-10, ls=-8, rl=10, rs=8, shapes=[("line", -30, 22, 50, 22, 4), ("circle", 32, 16, 9, 3)]),
      dict(dy=-6, head=12,  la=-130, lf=-60, ra=45, rf=85, ll=-10, ls=-8, rl=10, rs=8, shapes=[("line", -30, 22, 50, 22, 4), ("circle", 32, 16, 9, 3)]),
      dict(dy=-6, head=-12, la=-130, lf=-60, ra=25, rf=60, ll=-10, ls=-8, rl=10, rs=8, shapes=[("line", -30, 22, 50, 22, 4), ("circle", 32, 16, 9, 3)]),
      dict(dy=-6, head=12,  la=-120, lf=-50, ra=45, rf=80, ll=-10, ls=-8, rl=10, rs=8, shapes=[("line", -30, 22, 50, 22, 4), ("circle", 32, 16, 9, 3)]),
  ]),
  ("jump", "Jumping", 180, [
      dict(dy=10, lean=12, la=40,  lf=70,  ra=20,  rf=50,  ll=-45, ls=30, rl=45, rs=-30),
      dict(dy=-10, la=-160, lf=-170, ra=160, rf=170, ll=-12, ls=-8, rl=12, rs=8),
      dict(dy=-24, la=-165, lf=-175, ra=165, rf=175, ll=-60, ls=40, rl=60, rs=-40),
      dict(dy=-10, la=-150, lf=-160, ra=150, rf=160, ll=-15, ls=-10, rl=15, rs=10),
      dict(dy=10, lean=12, la=40,  lf=70,  ra=20,  rf=50,  ll=-45, ls=30, rl=45, rs=-30),
  ]),
  ("sunsalute", "Sun salutation", 900, [
      dict(la=-12, lf=-8, ra=12, rf=8, ll=-8, ls=-8, rl=8, rs=8),                                   # mountain
      dict(la=-170, lf=-175, ra=170, rf=175, ll=-8, ls=-8, rl=8, rs=8),                             # arms up
      dict(lean=85, head=-30, la=-10, lf=0, ra=10, rf=0, ll=-8, ls=-8, rl=8, rs=8),                 # forward fold
      dict(lean=90, head=-45, dy=-14, la=0, lf=0, ra=5, rf=0, ll=-100, ls=-100, rl=-100, rs=-100),  # plank
      dict(lean=55, head=-30, dy=-6, la=0, lf=0, ra=5, rf=0, ll=-110, ls=-105, rl=-110, rs=-105),   # upward dog
      dict(lean=125, head=20, dy=-20, la=0, lf=0, ra=5, rf=0, ll=-150, ls=-160, rl=-150, rs=-160),  # downward dog
  ]),
  ("bike", "Riding a bike", 160, [
      dict(dy=-8, lean=35, head=-20, la=55, lf=85, ra=50, rf=80, ll=-25, ls=25, rl=35, rs=0,
           shapes=[("circle", -26, 40, 14, 3), ("circle", 26, 40, 14, 3), ("line", -26, 40, 0, 40, 3), ("line", 0, 40, 26, 40, 3), ("line", -26, 40, 0, 0, 3), ("line", 0, 0, 24, 8, 3), ("line", 24, 8, 26, 40, 3), ("line", 20, 4, 34, 2, 3)]),
      dict(dy=-8, lean=35, head=-20, la=55, lf=85, ra=50, rf=80, ll=5, ls=45, rl=25, rs=-25,
           shapes=[("circle", -26, 40, 14, 3), ("circle", 26, 40, 14, 3), ("line", -26, 40, 0, 40, 3), ("line", 0, 40, 26, 40, 3), ("line", -26, 40, 0, 0, 3), ("line", 0, 0, 24, 8, 3), ("line", 24, 8, 26, 40, 3), ("line", 20, 4, 34, 2, 3)]),
      dict(dy=-8, lean=35, head=-20, la=55, lf=85, ra=50, rf=80, ll=35, ls=0, rl=-25, rs=25,
           shapes=[("circle", -26, 40, 14, 3), ("circle", 26, 40, 14, 3), ("line", -26, 40, 0, 40, 3), ("line", 0, 40, 26, 40, 3), ("line", -26, 40, 0, 0, 3), ("line", 0, 0, 24, 8, 3), ("line", 24, 8, 26, 40, 3), ("line", 20, 4, 34, 2, 3)]),
      dict(dy=-8, lean=35, head=-20, la=55, lf=85, ra=50, rf=80, ll=25, ls=-25, rl=5, rs=45,
           shapes=[("circle", -26, 40, 14, 3), ("circle", 26, 40, 14, 3), ("line", -26, 40, 0, 40, 3), ("line", 0, 40, 26, 40, 3), ("line", -26, 40, 0, 0, 3), ("line", 0, 0, 24, 8, 3), ("line", 24, 8, 26, 40, 3), ("line", 20, 4, 34, 2, 3)]),
  ]),
  ("dancing", "Dancing", 220, [
      dict(lean=-10, la=-150, lf=-170, ra=70, rf=30, ll=-25, ls=-15, rl=20, rs=15),
      dict(lean=10, la=-70, lf=-30, ra=150, rf=170, ll=-20, ls=-15, rl=45, rs=-35),
      dict(lean=-6, la=-165, lf=-175, ra=165, rf=175, ll=-35, ls=25, rl=35, rs=-25),
      dict(lean=8, la=-100, lf=-165, ra=100, rf=165, ll=-50, ls=40, rl=15, rs=15),
      dict(lean=-12, la=-140, lf=-60, ra=-40, rf=-100, ll=-15, ls=-10, rl=55, rs=-70),
      dict(lean=14, la=40, lf=100, ra=140, rf=60, ll=-55, ls=70, rl=10, rs=10),
  ]),
]

# Discs crossing the light: 128x128 frames (the whole circle), a filled disc
# entering from one side and leaving by the other. (id, name, ms, radius, axis)
DISCS = [
  ("eclipse-small-lr", "Eclipse, small, left to right", 65, 22, "x"),
  ("eclipse-small-tb", "Eclipse, small, top to bottom", 65, 22, "y"),
  ("eclipse-lr",       "Eclipse, full, left to right",  65, 64, "x"),
  ("eclipse-tb",       "Eclipse, full, top to bottom",  65, 64, "y"),
]
DISC_FRAMES = 24

def disc_frames(radius, axis):
    out = []
    for i in range(DISC_FRAMES):
        img = Image.new("1", (128, 128), 0)
        t = i / (DISC_FRAMES - 1)
        c = -radius + t * (128 + 2 * radius)          # centre travels from fully outside to fully outside
        cx, cy = (c, 63.5) if axis == "x" else (63.5, c)
        ImageDraw.Draw(img).ellipse([cx - radius, cy - radius, cx + radius, cy + radius], fill=1)
        out.append(img)
    return out

def pack(img):
    w, h = img.size
    out, stride = [], (w + 7) // 8
    px = img.load()
    for y in range(h):
        for b in range(stride):
            byte = 0
            for k in range(8):
                x = b * 8 + k
                byte = (byte << 1) | (1 if x < w and px[x, y] else 0)
            out.append(byte)
    return out

lines = ["// Generated by tools/gen_sprites.py. Do not edit.", "#pragma once", "#include \"../Sprite.h\"", ""]
refs = []
sheet = []
for sid, name, ms, poses in SPRITES:
    frames = render(poses)
    sheet.append((name, frames))
    data = []
    for f in frames: data += pack(f)
    lines.append(f"static const uint8_t sprite_{sid}_data[] PROGMEM = {{")
    for i in range(0, len(data), 24):
        lines.append("  " + ",".join(f"0x{b:02X}" for b in data[i:i+24]) + ",")
    lines.append("};")
    lines.append(f'static const Sprite sprite_{sid} = {{"{sid}", "{name}", {W}, {H}, {len(frames)}, {ms}, sprite_{sid}_data}};')
    lines.append("")
    refs.append(f"&sprite_{sid}")
for sid, name, ms, radius, axis in DISCS:
    frames = disc_frames(radius, axis)
    sheet.append((name, [f.resize((64, 64)) for f in frames]))
    data = []
    for f in frames: data += pack(f)
    lines.append(f"static const uint8_t sprite_{sid.replace('-', '_')}_data[] PROGMEM = {{")
    for i in range(0, len(data), 24):
        lines.append("  " + ",".join(f"0x{b:02X}" for b in data[i:i+24]) + ",")
    lines.append("};")
    lines.append(f'static const Sprite sprite_{sid.replace("-", "_")} = {{"{sid}", "{name}", 128, 128, {len(frames)}, {ms}, sprite_{sid.replace("-", "_")}_data}};')
    lines.append("")
    refs.append(f"&sprite_{sid.replace('-', '_')}")
    print(f"disc {sid}: {len(frames)} frames, {len(data)} bytes")
lines.append(f"static const Sprite* const SPRITES[] = {{{', '.join(refs)}}};")
lines.append(f"static const int SPRITE_COUNT = {len(refs)};")
open(OUT, "w").write("\n".join(lines) + "\n")

# contact sheet for a look
cols = max(len(f) for _, f in sheet)
img = Image.new("RGB", (cols * (W + 8) + 8, len(sheet) * (H + 8) + 8), (40, 40, 40))
for r, (name, frames) in enumerate(sheet):
    for c, f in enumerate(frames):
        tile = Image.new("RGB", (W, H), (210, 50, 30))
        fw, fh = f.size
        tile.paste((0, 0, 0), ((W - fw) // 2, (H - fh) // 2), mask=f.convert("1"))
        img.paste(tile, (8 + c * (W + 8), 8 + r * (H + 8)))
sheet_path = os.path.join(os.environ.get("SHEET_DIR", "."), "sprites.png")
img.resize((img.width * 2, img.height * 2), Image.NEAREST).save(sheet_path)
print(f"wrote {OUT} and {sheet_path}")
