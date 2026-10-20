#!/usr/bin/env python3
"""Animation file format version 3 ("SAN" 0x03): context-model arithmetic
coding of the sign's 7-bit pixels.  Encoder and a (slow, reference) decoder;
the board decodes with src/AnimV3.cpp, which must stay bit-exact with this.

    encode(w, h, delays, frames, temporal=None, K=None, ctxset=None) -> bytes
    decode(data) -> (w, h, delays, frames)
    python3 tools/anim_v3.py in.anm out.anm      # re-encode a v1/v2/v3 file
    python3 tools/anim_v3.py --prepare samples_dir out_dir
        # every .anm -> out_dir/<name>.v3 and <name>.raw (reference frames
        # for tools/anim_v3_test.cpp: u16 w, h, n then the raw frames)

File layout and the coding rules are described in src/AnimV3.h /
src/AnimV3.cpp.  In short, every pixel is coded in raster order as
  temporal frame:  bit "same as the previous frame's pixel"
  if l1 == u1 == l4 == u4:  bit "same as the left pixel"
  else: three 2-bit channels (contexts: the channel of four neighbours and
        the channel just decoded) and the light bit
with an adaptive binary range coder.  Every frame starts from a per-clip
"prior" (model trained over the whole clip in a first pass, 8 bits per
state), so frames depend on nothing but their own bytes and, for temporal
frames, the previous frame.  Keyframes (frame % K == 0) are never temporal.
Masked clips (header flag bit 1, the default for 128-wide clips of 128-row
tiles): pixels outside each tile's circle (128 px plus a 2 px margin) are
not coded at all and decode as black; the sign's cover hides them.
"""
import os, struct, sys
from multiprocessing import Pool

import numpy as np

VERSION = 3
OFF_T, OFF_S, OFF_R = 0, 16, 25
OFF_G = OFF_R + 256 * 3
OFF_B = OFF_G + 1024 * 3
OFF_L = OFF_B + 1024 * 3
STATES = OFF_L + 512
INIT = 2048 << 4
TOP = 1 << 24


# ------------------------------------------------------------ modelling
def _shift(a, dx, dy):
    """a[y - dy, x - dx] with zeros outside (dx may be negative = right)."""
    h, w = a.shape
    out = np.zeros_like(a)
    ys, yd = (slice(dy, None), slice(0, h - dy)) if dy >= 0 else (slice(0, h + dy), slice(-dy, None))
    xs, xd = (slice(dx, None), slice(0, w - dx)) if dx >= 0 else (slice(0, w + dx), slice(-dx, None))
    out[ys, xs] = a[yd, xd]
    return out


MASK_DIAMETER = 132   # the 128 px circle plus a 2 px margin


def mask_array(w, h):
    """True where a pixel is coded (inside the circle of each 128-row tile),
    None when the frame is not made of 128x128 tiles.  Mirrors maskSpan in
    AnimV3.cpp: pixel (x, y) coded iff (2x-127)^2 + (2(y%128)-127)^2 <= 132^2."""
    if w != 128 or h % 128:
        return None
    ys, xs = np.mgrid[0:128, 0:128]
    dx, dy = 2 * xs - 127, 2 * ys - 127
    return np.tile(dx * dx + dy * dy <= MASK_DIAMETER ** 2, (h // 128, 1))


def apply_mask(frames, w, h):
    """Blacks the pixels a masked clip does not code (what the decoder gives)."""
    m = mask_array(w, h)
    if m is None:
        return [np.ascontiguousarray(f, dtype=np.uint8).reshape(h, w) for f in frames]
    return [np.where(m, np.asarray(f, dtype=np.uint8).reshape(h, w), 0).astype(np.uint8) for f in frames]


def decisions(cur, prev, ctxset, temporal, mask=None):
    """All binary decisions of one frame in coding order -> (state indices, bits),
    both int64 arrays.  Everything a decoder derives from already decoded
    pixels is a pure function of the frame data, so it vectorises.  mask:
    bool array of the coded pixels (hidden ones are 0, get no decisions and
    carry tf = 0, sf = 2 as context) or None."""
    v = cur.astype(np.int64)
    h, w = v.shape
    l1, u1, l4, u4 = _shift(v, 1, 0), _shift(v, 0, 1), _shift(v, 4, 0), _shift(v, 0, 4)
    if ctxset == 0:
        n2, n3 = l4, u4
    else:
        n2, n3 = _shift(v, 1, 1), _shift(v, -1, 1)
    npx = h * w
    idx = np.full((npx, 9), -1, np.int64)
    bits = np.zeros((npx, 9), np.int64)
    if temporal:
        t = prev.astype(np.int64)
        thit = v == t
        tf = thit.astype(np.int64)
        if mask is not None:
            tf[~mask] = 0
        tl, tu = _shift(tf, 1, 0), _shift(tf, 0, 1)
        idx[:, 0] = (OFF_T + tl * 8 + tu * 4 + (l1 == t) * 2 + (u1 == t)).ravel()
        bits[:, 0] = tf.ravel()
    else:
        t = np.zeros_like(v)
        thit = np.zeros(v.shape, bool)
    agree = (l1 == u1) & (l1 == l4) & (l1 == u4) & ~thit
    shit = agree & (v == l1)
    sf = np.where(thit, 2, np.where(agree, shit.astype(np.int64), 2))
    if mask is not None:
        sf[~mask] = 2
    sl, su = _shift(sf, 1, 0), _shift(sf, 0, 1)
    sidx = OFF_S + sl * 3 + su
    idx[:, 1] = np.where(agree, sidx, -1).ravel()
    bits[:, 1] = shit.ravel()
    lit = (~thit & ~shit).ravel()
    prevch = np.zeros(npx, np.int64)
    for ch in range(3):
        sh = 4 - 2 * ch
        c = (((l1 >> sh) & 3) << 6) | (((u1 >> sh) & 3) << 4) | (((n2 >> sh) & 3) << 2) | ((n3 >> sh) & 3)
        c = c.ravel()
        val = ((v >> sh) & 3).ravel()
        hi, lo = val >> 1, val & 1
        base = OFF_R + c * 3 if ch == 0 else (OFF_G if ch == 1 else OFF_B) + ((c << 2) | prevch) * 3
        idx[:, 2 + 2 * ch] = np.where(lit, base, -1)
        bits[:, 2 + 2 * ch] = hi
        idx[:, 3 + 2 * ch] = np.where(lit, base + 1 + hi, -1)
        bits[:, 3 + 2 * ch] = lo
        prevch = val
    tlight = ((t >> 6) & 1) if temporal else np.zeros_like(v)
    lidx = OFF_L + ((l1 >> 6) & 1) * 256 + ((u1 >> 6) & 1) * 128 + tlight * 64 + (v & 63)
    idx[:, 8] = np.where(lit, lidx.ravel(), -1)
    bits[:, 8] = ((v >> 6) & 1).ravel()
    if mask is not None:
        idx[~mask.ravel(), :] = -1
    keep = idx.ravel() >= 0
    return idx.ravel()[keep], bits.ravel()[keep]


def _train(st, idx, bits):
    """Adapt the model states over the decisions (no coder)."""
    for i, b in zip(idx.tolist(), bits.tolist()):
        s = st[i]
        p, n = s >> 4, s & 15
        sh = n + 1 if n < 5 else 5
        if b:
            p -= (p - 16) >> sh
        else:
            p += (4080 - p) >> sh
        st[i] = (p << 4) | (n + 1 if n < 15 else 15)


class _Encoder:
    """LZMA-style binary range encoder."""
    __slots__ = ("low", "rng", "cache", "cache_size", "out")

    def __init__(self):
        self.low, self.rng, self.cache, self.cache_size, self.out = 0, 0xFFFFFFFF, 0, 1, bytearray()

    def shift_low(self):
        low = self.low
        if low < 0xFF000000 or low >= 0x100000000:
            carry = low >> 32
            temp = self.cache
            out = self.out
            while True:
                out.append((temp + carry) & 0xFF)
                temp = 0xFF
                self.cache_size -= 1
                if self.cache_size == 0:
                    break
            self.cache = (low >> 24) & 0xFF
        self.cache_size += 1
        self.low = (low & 0x00FFFFFF) << 8

    def run(self, st, idx, bits):
        low, rng = self.low, self.rng
        for i, b in zip(idx.tolist(), bits.tolist()):
            s = st[i]
            p, n = s >> 4, s & 15
            sh = n + 1 if n < 5 else 5
            bound = (rng >> 12) * p
            if b:
                low += bound
                rng -= bound
                p -= (p - 16) >> sh
            else:
                rng = bound
                p += (4080 - p) >> sh
            st[i] = (p << 4) | (n + 1 if n < 15 else 15)
            while rng < TOP:
                rng <<= 8
                self.low = low
                self.shift_low()
                low = self.low
        self.low, self.rng = low, rng

    def finish(self):
        for _ in range(5):
            self.shift_low()
        return bytes(self.out)


def _code_frame(prior, idx, bits):
    st = [(q << 8) | 3 for q in prior]
    e = _Encoder()
    e.run(st, idx, bits)
    return e.finish()


def _encode_frame(args):
    """One frame from the prior: picks temporal or not (whichever is smaller)."""
    prior, cur, prev, ctxset, allow_t, mask = args
    best = None
    for temporal in ([False, True] if allow_t else [False]):
        idx, bits = decisions(cur, prev, ctxset, temporal, mask)
        data = bytes([1 if temporal else 0]) + _code_frame(prior, idx, bits)
        if best is None or len(data) < len(best):
            best = data
    return best


def _pick_ctxset(frames, prev_ok, mask):
    """Cheaper proxy for the clip: the context set whose literal contexts are
    more deterministic, measured on a few frames with a fresh model."""
    sizes = []
    sample = frames[:: max(1, len(frames) // 6)][:6]
    for ctxset in (0, 1):
        st = [INIT] * STATES
        total = 0
        for i, f in enumerate(sample):
            idx, bits = decisions(f, None, ctxset, False, mask)
            e = _Encoder()
            st2 = list(st)
            e.run(st2, idx, bits)
            total += len(e.finish())
            st = st2
        sizes.append(total)
    return 0 if sizes[0] <= sizes[1] else 1


def encode(w, h, delays, frames, temporal=None, K=None, ctxset=None, workers=None, masked=True):
    """frames: list of h x w uint8 arrays.  temporal defaults to on for
    clips of at most 16 K pixels per frame (the board keeps the previous
    frame then), K to 8 with temporal frames and 1 without.  masked (128-wide
    clips of 128-row tiles only): the pixels outside each tile's circle are
    not coded at all and decode as black (the sign's cover hides them)."""
    n = len(frames)
    assert n == len(delays) and 0 < n < 65536 and 0 < w <= 128 and h > 0
    mask = mask_array(w, h) if masked else None
    if mask is not None:
        frames = apply_mask(frames, w, h)
    else:
        frames = [np.ascontiguousarray(f, dtype=np.uint8).reshape(h, w) for f in frames]
    if temporal is None:
        temporal = w * h <= 16384
    if K is None:
        K = 8 if temporal else 1
    if ctxset is None:
        ctxset = _pick_ctxset(frames, temporal, mask)
    # pass 1: train the prior over the whole clip
    st = [INIT] * STATES
    for i, f in enumerate(frames):
        t = temporal and i % K != 0
        idx, bits = decisions(f, frames[i - 1] if t else None, ctxset, t, mask)
        _train(st, idx, bits)
    prior = bytes([max(1, s >> 8) for s in st])
    # pass 2: code every frame from the prior (independent, so in parallel)
    jobs = [(prior, frames[i], frames[i - 1] if i else None, ctxset, temporal and i % K != 0, mask) for i in range(n)]
    if workers == 1 or n == 1:
        blobs = [_encode_frame(j) for j in jobs]
    else:
        with Pool(workers) as pool:
            blobs = pool.map(_encode_frame, jobs, chunksize=1)
    nkeys = (n + K - 1) // K
    flags = (1 if any(b[0] & 1 for b in blobs) else 0) | (2 if mask is not None else 0)
    out = bytearray(b"SAN\x03" + struct.pack("<HHHH", w, h, n, 0))
    out += struct.pack("<BBBBHH", K, ctxset, flags, 0, nkeys, STATES)
    for d, b in zip(delays, blobs):
        out += struct.pack("<HI", d, len(b))
    data_start = len(out) + 4 * nkeys + STATES
    off, key_offsets = data_start, []
    for i, b in enumerate(blobs):
        if i % K == 0:
            key_offsets.append(off)
        off += len(b)
    for o in key_offsets:
        out += struct.pack("<I", o)
    out += prior
    for b in blobs:
        out += b
    return bytes(out)


# ------------------------------------------------------------ reference decoder
class _Decoder:
    def __init__(self, data):
        self.data, self.pos = data, 0
        self.rng, self.code = 0xFFFFFFFF, 0
        for _ in range(5):
            self.code = (self.code << 8) | self.byte()

    def byte(self):
        b = self.data[self.pos] if self.pos < len(self.data) else 0
        self.pos += 1
        return b

    def bit(self, st, i):
        s = st[i]
        p, n = s >> 4, s & 15
        sh = n + 1 if n < 5 else 5
        bound = (self.rng >> 12) * p
        if self.code < bound:
            self.rng = bound
            b = 0
            p += (4080 - p) >> sh
        else:
            self.code -= bound
            self.rng -= bound
            b = 1
            p -= (p - 16) >> sh
        st[i] = (p << 4) | (n + 1 if n < 15 else 15)
        while self.rng < TOP:
            self.rng <<= 8
            self.code = ((self.code << 8) | self.byte()) & 0xFFFFFFFF
        return b


def decode_frame(prior, blob, w, h, ctxset, prev, mask=None):
    """Pure-Python mirror of decodeOne in AnimV3.cpp (slow: for checks)."""
    temporal = blob[0] & 1
    if temporal and prev is None:
        raise ValueError("temporal frame without a previous frame")
    st = [(q << 8) | 3 for q in prior]
    d = _Decoder(blob[1:])
    cur = np.zeros((h, w), np.uint8)
    tf = np.zeros((h, w), np.uint8)
    sf = np.zeros((h, w), np.uint8)
    px = lambda x, y: int(cur[y, x]) if 0 <= x < w and 0 <= y < h else 0
    for y in range(h):
        for x in range(w):
            if mask is not None and not mask[y, x]:
                tf[y, x], sf[y, x] = 0, 2   # hidden: black, no decisions
                continue
            l1, u1, l4, u4 = px(x - 1, y), px(x, y - 1), px(x - 4, y), px(x, y - 4)
            tl = int(tf[y, x - 1]) if x else 0
            tu = int(tf[y - 1, x]) if y else 0
            sl = int(sf[y, x - 1]) if x else 0
            su = int(sf[y - 1, x]) if y else 0
            if temporal:
                t = int(prev[y, x])
                hit = d.bit(st, OFF_T + tl * 8 + tu * 4 + (l1 == t) * 2 + (u1 == t))
                tf[y, x] = hit
                if hit:
                    cur[y, x] = t
                    sf[y, x] = 2
                    continue
            if l1 == u1 == l4 == u4:
                hit = d.bit(st, OFF_S + sl * 3 + su)
                sf[y, x] = 1 if hit else 0
                if hit:
                    cur[y, x] = l1
                    continue
            else:
                sf[y, x] = 2
            if ctxset == 0:
                n2, n3 = l4, u4
            else:
                n2, n3 = px(x - 1, y - 1), px(x + 1, y - 1)
            colour, prevch = 0, 0
            for ch in range(3):
                sh = 4 - 2 * ch
                c = (((l1 >> sh) & 3) << 6) | (((u1 >> sh) & 3) << 4) | (((n2 >> sh) & 3) << 2) | ((n3 >> sh) & 3)
                base = OFF_R + c * 3 if ch == 0 else (OFF_G if ch == 1 else OFF_B) + ((c << 2) | prevch) * 3
                hi = d.bit(st, base)
                lo = d.bit(st, base + 1 + hi)
                prevch = hi * 2 + lo
                colour = (colour << 2) | prevch
            tlight = (int(prev[y, x]) >> 6) & 1 if temporal else 0
            light = d.bit(st, OFF_L + ((l1 >> 6) & 1) * 256 + ((u1 >> 6) & 1) * 128 + tlight * 64 + colour)
            cur[y, x] = (light << 6) | colour
    return cur


def parse(data):
    """-> dict with the header fields, delays, frame blobs and prior."""
    if data[:4] != b"SAN\x03":
        raise ValueError("not a version 3 animation")
    w, h, n, _ = struct.unpack_from("<HHHH", data, 4)
    K, ctxset, flags, _, nkeys, pbytes = struct.unpack_from("<BBBBHH", data, 12)
    if pbytes != STATES:
        raise ValueError("unexpected model size")
    if flags & ~3:
        raise ValueError("unknown format 3 flags")
    if flags & 2 and (w != 128 or h % 128):
        raise ValueError("masked clip must be 128 wide and tiled in 128 rows")
    delays, lens = [], []
    for i in range(n):
        d, ln = struct.unpack_from("<HI", data, 20 + 6 * i)
        delays.append(d)
        lens.append(ln)
    o = 20 + 6 * n
    keys = list(struct.unpack_from("<%dI" % nkeys, data, o))
    o += 4 * nkeys
    prior = data[o:o + STATES]
    o += STATES
    blobs = []
    for ln in lens:
        blobs.append(data[o:o + ln])
        o += ln
    return dict(w=w, h=h, n=n, K=K, ctxset=ctxset, flags=flags, keys=keys, delays=delays, prior=prior, blobs=blobs)


def decode(data, frames_wanted=None):
    """-> (w, h, delays, frames).  frames_wanted: optional set of indices to
    decode (others come back as None); their keyframe groups are decoded."""
    p = parse(data)
    w, h, K = p["w"], p["h"], p["K"]
    frames = [None] * p["n"]
    prev = None
    mask = mask_array(w, h) if p["flags"] & 2 else None
    for i, blob in enumerate(p["blobs"]):
        if frames_wanted is not None and not any(j in frames_wanted for j in range(i, min(p["n"], i - i % K + K))):
            prev = None
            continue
        if i % K == 0:
            prev = None
        cur = decode_frame(p["prior"], blob, w, h, p["ctxset"], prev, mask)
        frames[i] = cur
        prev = cur
    return w, h, p["delays"], frames


def load_any(path):
    """-> (w, h, delays, frames) from a v1/v2/v3 .anm file."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import anim_recompress as ar
    src = open(path, "rb").read()
    if src[3] == 3:
        return decode(src)
    w, h, delays, frames, _ = ar.decode(src)
    return w, h, delays, frames


def prepare(sdir, odir):
    import glob, time
    os.makedirs(odir, exist_ok=True)
    for p in sorted(glob.glob(os.path.join(sdir, "*.anm"))):
        name = os.path.basename(p)[:-4]
        w, h, delays, frames = load_any(p)
        t0 = time.time()
        out = encode(w, h, delays, frames)
        frames = apply_mask(frames, w, h)   # the reference is what the decoder must produce
        with open(os.path.join(odir, name + ".v3"), "wb") as f:
            f.write(out)
        with open(os.path.join(odir, name + ".raw"), "wb") as f:
            f.write(struct.pack("<HHH", w, h, len(frames)))
            for fr in frames:
                f.write(np.ascontiguousarray(fr, dtype=np.uint8).tobytes())
        print(f"{name}: {os.path.getsize(p)} -> {len(out)} bytes ({len(out) * 100 / os.path.getsize(p):.1f}%) in {time.time() - t0:.0f}s", flush=True)


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "--prepare":
        prepare(sys.argv[2], sys.argv[3])
        sys.exit(0)
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import anim_recompress as ar
    src = open(sys.argv[1], "rb").read()
    if src[3] == 3:
        w, h, delays, frames = decode(src)
    else:
        w, h, delays, frames, _ = ar.decode(src)
    out = encode(w, h, delays, frames)
    open(sys.argv[2], "wb").write(out)
    print(f"{len(src)} -> {len(out)} bytes ({len(out) * 100 / len(src):.1f}%)")
