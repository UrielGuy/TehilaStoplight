#!/usr/bin/env python3
"""Compression experiments for the sign's uploaded animations (.anm files).

    python3 tools/anim_codec_lab.py [samples_dir] [--quick] [--only name,name]

Decodes every .anm in the directory (default .pio/samples), encodes the
frames with every scheme below and prints a table of total bytes per
sample (payload only; the 6-byte-per-frame index table is the same for
all deflate schemes and is ignored) with the ratio against the current
version 2 format.  Nothing here touches the sign.

Pixel bytes: bits 0-5 = 0bRRGGBB (2 bits per channel), bit 6 = silhouette
light flag.  One-light clips are 128x128, whole-sign clips 128x384.

Schemes (all lossless):
  v2            per-frame raw deflate, 1 KB window (the current format)
  d4k d32k      per-frame raw deflate, bigger window
  g8_32k        8 frames per deflate stream (seek = decode up to 7 extra)
  pd32k         per-frame deflate with the previous frame as preset dictionary
  pal_*         clip-wide palette, pixels packed to 1/2/4/8 bits, then deflate
  planes        colour (6-bit) and light bit plane compressed separately
  f_<pred>      spatial/temporal predictor, residual "moved to zero", deflate
                (preds: l1 left, u1 up, l4 left by 4, u4 up by 4 = same Bayer
                phase, t previous frame, rowbest = encoder picks per row)
  pf_<pred>     the same on palette indices
  v3            the recommended codec: adaptive binary context-model
                arithmetic coder (tools/anim_v3.py, src/AnimV3.cpp)
  oracle_*      static conditional entropy with clip-wide tables (not a real
                coder, shows the headroom of context modelling)
"""
import glob, math, os, struct, sys, time, zlib

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import anim_recompress as ar


# ----------------------------------------------------------------- helpers
def deflate(b, wbits=-15, dictionary=None):
    if dictionary is not None:
        c = zlib.compressobj(9, zlib.DEFLATED, wbits, 9, zdict=dictionary)
    else:
        c = zlib.compressobj(9, zlib.DEFLATED, wbits, 9)
    return c.compress(bytes(b)) + c.flush()


def palette_of(frames):
    cols = np.unique(np.concatenate([f.ravel() for f in frames]))
    lut = np.zeros(128, np.uint8)
    lut[cols] = np.arange(len(cols), dtype=np.uint8)
    return cols, lut


def bits_for(n):
    return 1 if n <= 2 else 2 if n <= 4 else 4 if n <= 16 else 8


def pack_bits(idx, bpp):
    """idx: uint8 array (any shape) with values < 2**bpp -> packed bytes, MSB first."""
    flat = idx.ravel()
    if bpp == 8:
        return flat.tobytes()
    per = 8 // bpp
    pad = (-flat.size) % per
    if pad:
        flat = np.concatenate([flat, np.zeros(pad, np.uint8)])
    g = flat.reshape(-1, per).astype(np.uint16)
    out = np.zeros(g.shape[0], np.uint16)
    for k in range(per):
        out |= g[:, k] << (8 - bpp * (k + 1))
    return out.astype(np.uint8).tobytes()


def move_to_zero(p, pred):
    """Bijective residual: 0 when p == pred, else p+1 if p < pred else p (uint8 arrays)."""
    p = p.astype(np.int16)
    pred = pred.astype(np.int16)
    r = np.where(p == pred, 0, np.where(p < pred, p + 1, p))
    return r.astype(np.uint8)


def predictor(frame, prev, kind):
    h, w = frame.shape
    z = np.zeros_like(frame)
    if kind == "l1":
        z[:, 1:] = frame[:, :-1]
    elif kind == "u1":
        z[1:, :] = frame[:-1, :]
    elif kind == "l4":
        z[:, 4:] = frame[:, :-4]
        z[:, :4] = 0
    elif kind == "u4":
        z[4:, :] = frame[:-4, :]
    elif kind == "t":
        z = prev if prev is not None else z
    elif kind == "u4l4":   # gradient-style: up4 when it equals its left4, else left4
        u = np.zeros_like(frame); u[4:, :] = frame[:-4, :]
        l = np.zeros_like(frame); l[:, 4:] = frame[:, :-4]
        ul = np.zeros_like(frame); ul[4:, 4:] = frame[:-4, :-4]
        z = np.where(ul == l, u, l)
    return z


def filtered(frames, kind, wbits=-15, per_frame=True):
    """Apply predictor + move-to-zero and deflate.  'rowbest' lets the encoder pick
    the predictor per row (one choice byte per row)."""
    kinds = ["l1", "u1", "l4", "u4", "t"] if kind == "rowbest" else [kind]
    total = 0
    prev = None
    for f in frames:
        if kind == "rowbest":
            rows = []
            resids = {k: move_to_zero(f, predictor(f, prev, k)) for k in kinds}
            # cheap proxy for cost: count of non-zero residuals + distinct values
            best_rows = np.zeros(f.shape, np.uint8)
            choice = []
            for y in range(f.shape[0]):
                best = min(kinds, key=lambda k: np.count_nonzero(resids[k][y]))
                best_rows[y] = resids[best][y]
                choice.append(kinds.index(best))
            payload = bytes(choice) + best_rows.tobytes()
        else:
            payload = move_to_zero(f, predictor(f, prev, kind)).tobytes()
        total += len(deflate(payload, wbits))
        prev = f
    return total


def entropy_bits(counts):
    c = counts[counts > 0].astype(np.float64)
    p = c / c.sum()
    return float(-(c * np.log2(p)).sum())


def oracle(frames, ctx_fn):
    """Static conditional entropy H(pixel | ctx) with clip-wide tables, in bytes."""
    from collections import Counter
    sym_all, ctx_all = [], []
    prev = None
    for f in frames:
        ctx = ctx_fn(f, prev)
        sym_all.append(f.ravel().astype(np.int64))
        ctx_all.append(ctx.ravel().astype(np.int64))
        prev = f
    s = np.concatenate(sym_all)
    c = np.concatenate(ctx_all)
    key = c * 128 + s
    _, counts = np.unique(key, return_counts=True)
    _, ccounts = np.unique(c, return_counts=True)
    bits = entropy_bits(counts) - entropy_bits(ccounts)
    # H(s|c) = H(s,c) - H(c)
    return bits / 8


def ctx_l4u4(f, prev):
    l = np.zeros_like(f); l[:, 4:] = f[:, :-4]
    u = np.zeros_like(f); u[4:, :] = f[:-4, :]
    return l.astype(np.int64) * 128 + u


def ctx_l1u1(f, prev):
    l = np.zeros_like(f); l[:, 1:] = f[:, :-1]
    u = np.zeros_like(f); u[1:, :] = f[:-1, :]
    return l.astype(np.int64) * 128 + u


def ctx_l4u4t(f, prev):
    t = prev if prev is not None else np.zeros_like(f)
    return ctx_l4u4(f, prev) * 128 + t


# ----------------------------------------------------------------- schemes
def scheme_v2(fr, w, h):
    return sum(len(deflate(f.tobytes(), -10)) for f in fr)


def scheme_dw(wbits):
    return lambda fr, w, h: sum(len(deflate(f.tobytes(), wbits)) for f in fr)


def scheme_group(n, wbits=-15):
    def run(fr, w, h):
        t = 0
        for i in range(0, len(fr), n):
            t += len(deflate(b"".join(f.tobytes() for f in fr[i:i + n]), wbits))
        return t
    return run


def scheme_prevdict(fr, w, h):
    t, prev = 0, None
    for f in fr:
        d = prev.tobytes()[-32768:] if prev is not None else None
        t += len(deflate(f.tobytes(), -15, d))
        prev = f
    return t


def scheme_palette(wbits):
    def run(fr, w, h):
        cols, lut = palette_of(fr)
        bpp = bits_for(len(cols))
        return len(cols) + sum(len(deflate(pack_bits(lut[f], bpp), wbits)) for f in fr)
    return run


def scheme_planes(fr, w, h):
    t = 0
    for f in fr:
        t += len(deflate((f & 63).tobytes(), -15))
        t += len(deflate(pack_bits((f >> 6) & 1, 1), -15))
    return t


def scheme_filter(kind, wbits=-15, on_palette=False):
    def run(fr, w, h):
        frames = fr
        if on_palette:
            cols, lut = palette_of(fr)
            frames = [lut[f] for f in fr]
        return filtered(frames, kind, wbits)
    return run


def scheme_oracle(ctx_fn):
    return lambda fr, w, h: oracle(fr, ctx_fn)


def scheme_v3(sdir):
    """The recommended codec (tools/anim_v3.py).  Uses <sdir>/../v3/<name>.v3
    when `python3 tools/anim_v3.py --prepare` wrote it, else encodes now."""
    import anim_v3
    def run(fr, w, h, name=None):
        if name:
            cached = os.path.join(sdir, "..", "v3", name + ".v3")
            if os.path.exists(cached):
                return os.path.getsize(cached)
        return len(anim_v3.encode(w, h, [83] * len(fr), fr))
    return run


SCHEMES = [
    ("v2", scheme_v2),
    ("d4k", scheme_dw(-12)),
    ("d32k", scheme_dw(-15)),
    ("g8_32k", scheme_group(8)),
    ("g16_32k", scheme_group(16)),
    ("pd32k", scheme_prevdict),
    ("pal_1k", scheme_palette(-10)),
    ("pal_32k", scheme_palette(-15)),
    ("planes", scheme_planes),
    ("f_l1", scheme_filter("l1")),
    ("f_u1", scheme_filter("u1")),
    ("f_l4", scheme_filter("l4")),
    ("f_u4", scheme_filter("u4")),
    ("f_u4l4", scheme_filter("u4l4")),
    ("f_t", scheme_filter("t")),
    ("f_rowbest", scheme_filter("rowbest")),
    ("f_rowbest_1k", scheme_filter("rowbest", -10)),
    ("pf_u4", scheme_filter("u4", on_palette=True)),
    ("pf_rowbest", scheme_filter("rowbest", on_palette=True)),
    ("oracle_l1u1", scheme_oracle(ctx_l1u1)),
    ("oracle_l4u4", scheme_oracle(ctx_l4u4)),
    ("oracle_l4u4t", scheme_oracle(ctx_l4u4t)),
]
QUICK = {"v2", "d32k", "g8_32k", "pal_32k", "f_rowbest", "oracle_l4u4t", "v3"}


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    sdir = args[0] if args else os.path.join(HERE, "..", ".pio", "samples")
    quick = "--quick" in sys.argv
    only = None
    for a in sys.argv[1:]:
        if a.startswith("--only="):
            only = set(a[7:].split(","))
    SCHEMES.append(("v3", scheme_v3(sdir)))
    schemes = [(n, f) for n, f in SCHEMES if (only is None or n in only) and (not quick or n in QUICK)]

    samples = []
    for p in sorted(glob.glob(os.path.join(sdir, "*.anm"))):
        w, h, delays, frames, ver = ar.decode(open(p, "rb").read())
        samples.append((os.path.basename(p)[:-4], w, h, frames, os.path.getsize(p)))
    if not samples:
        sys.exit(f"no .anm files in {sdir}")
    print("sample            size  frames  raw bytes   file bytes")
    for name, w, h, fr, size in samples:
        print(f"{name:16s} {w}x{h} {len(fr):6d} {w * h * len(fr):10d} {size:10d}")
    print()

    base = {}
    results = []
    head = f"{'scheme':14s}" + "".join(f"{n[:12]:>13s}" for n, *_ in samples) + f"{'TOTAL':>12s}{'vs v2':>8s}"
    print(head)
    for sname, fn in schemes:
        row, total = [], 0
        t0 = time.time()
        for name, w, h, fr, size in samples:
            b = int(fn(fr, w, h, name) if sname == "v3" else fn(fr, w, h))
            row.append(b)
            total += b
            if sname == "v2":
                base[name] = b
        cells = "".join(f"{b:13d}" for b in row)
        ratio = f"{total / base['__t'] * 100:7.1f}%" if "__t" in base else ""
        if sname == "v2":
            base["__t"] = total
            ratio = "  100.0%"
        print(f"{sname:14s}{cells}{total:12d}{ratio}   ({time.time() - t0:.0f}s)")
        sys.stdout.flush()
        results.append((sname, row))
    print()
    print("per-sample size as % of v2 (smaller is better):")
    print(f"{'scheme':14s}" + "".join(f"{n[:12]:>13s}" for n, *_ in samples))
    for sname, row in results:
        print(f"{sname:14s}" + "".join(f"{b / base[n] * 100:12.1f}%" for b, (n, *_) in zip(row, samples)))


if __name__ == "__main__":
    main()
