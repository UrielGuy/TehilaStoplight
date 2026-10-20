#!/usr/bin/env python3
"""Re-encode the animations stored on the sign in the latest format (version 3,
context-model coding, ~2.5x smaller than run-length v1 and ~30% smaller than
deflate v2) and put them back under the same names.

    python.exe tools/anim_recompress.py            # sign on a USB cable (auto)
    python.exe tools/anim_recompress.py COM19
    python.exe tools/anim_recompress.py http://192.168.4.1
    python.exe tools/anim_recompress.py --dry-run  # back up and report only

Every file is first copied to tools/gfx/backup/<date-time>/, the new file is
checked to decode to exactly the same pixels, and the sign swaps it in only
after the whole upload arrived with a matching checksum. Files already in
masked version 3 are left alone; the pixels outside each light's circle are
dropped (they decode as black, the sign's cover hides them).
"""
import base64, datetime, json, os, struct, sys, time, urllib.request, zlib

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import anim_convert as ac
import anim_v3

HERE = os.path.dirname(os.path.abspath(__file__))


def list_serial(s):
    s.write(b"\n#ANIM LIST\n")
    r = ac.serial_reply(s, 5)
    if not r or not r.startswith("LIST "):
        raise RuntimeError(f"no list from the sign ({r})")
    return json.loads(r[5:])


def fetch_serial(s, name, size):
    out, off, tries = bytearray(), 0, 0
    while off < size:
        n = min(768, size - off)
        s.write(f"\n#ANIM GET {name} {off} {n}\n".encode())
        r = ac.serial_reply(s, 1.2)
        ok = False
        if r and r.startswith("BLK "):
            try:   # the native USB link occasionally drops bytes: a damaged block is just read again
                _, o, ln, crc, b64 = r.split(" ", 4)
                data = base64.b64decode(b64)
                ok = int(o) == off and len(data) == int(ln) == n and zlib.crc32(data) & 0xFFFFFFFF == int(crc, 16)
            except ValueError:
                ok = False
        elif r and r.startswith("ERR"):
            raise RuntimeError(f"{name}: {r[4:]}")
        if ok:
            out += data
            off += n
            tries = 0
            ac.print_progress("download", off, size, "bytes")
        else:
            tries += 1
            if tries > 25:
                raise RuntimeError(f"{name}: read keeps failing at byte {off}")
    return bytes(out)


def decode(data):
    """-> (w, h, delays, [pixel arrays], version); version 3 with the circle
    mask flag reports as 3.5 so callers can tell it from the unmasked kind."""
    if data[:3] != b"SAN":
        raise ValueError("not an animation")
    ver = data[3]
    if ver == 3:
        w, h, delays, frames = anim_v3.decode(data)
        return w, h, list(delays), [np.asarray(f, dtype=np.uint8).reshape(h, w) for f in frames], (3.5 if data[14] & 2 else 3)
    w, h, n, _ = struct.unpack_from("<HHHH", data, 4)
    delays, lens = [], []
    for i in range(n):
        d, ln = struct.unpack_from("<HI", data, 12 + 6 * i)
        delays.append(d)
        lens.append(ln)
    o, frames = 12 + 6 * n, []
    for ln in lens:
        blob = data[o:o + ln]
        o += ln
        if ver == 2:
            px = np.frombuffer(zlib.decompress(blob, -15), dtype=np.uint8)
        else:
            counts = np.frombuffer(blob[0::2], dtype=np.uint8)
            vals = np.frombuffer(blob[1::2], dtype=np.uint8)
            px = np.repeat(vals, counts)
        if px.size != w * h:
            raise ValueError(f"frame decodes to {px.size} pixels, expected {w * h}")
        frames.append(px.reshape(h, w))
    return w, h, delays, frames, ver


def encode_best(w, h, delays, frames):
    """Format 3, unless per-frame deflate (format 2) is smaller for this clip.
    Both carry the masked pixels (black outside the circles)."""
    frames = anim_v3.apply_mask(frames, w, h)
    v3 = anim_v3.encode(w, h, delays, frames)
    blobs = [ac.deflate(p) for p in frames]
    v2 = bytearray(b"SAN\x02" + struct.pack("<HHHH", w, h, len(frames), 0))
    for d, b in zip(delays, blobs):
        v2 += struct.pack("<HI", d, len(b))
    for b in blobs:
        v2 += b
    return v3 if len(v3) <= len(v2) else bytes(v2)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    dry = "--dry-run" in sys.argv
    target = args[0] if args else "usb"
    backup = os.path.join(HERE, "gfx", "backup", datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
    os.makedirs(backup, exist_ok=True)

    s = None
    if target.startswith("http"):
        anims = json.loads(urllib.request.urlopen(target.rstrip("/") + "/api/anims", timeout=10).read())["anims"]
        get = lambda name, size: urllib.request.urlopen(f"{target.rstrip('/')}/api/anims/{name}.anm", timeout=60).read()
    else:
        port = target
        if port in ("usb", "auto"):
            ports = ac.sign_ports()
            if not ports:
                sys.exit("no sign found on a USB port")
            port = ports[0][0]
        s = ac.serial_link(port)
        s.reset_input_buffer()
        anims = list_serial(s)
        get = lambda name, size: fetch_serial(s, name, size)
        target = port

    if not anims:
        sys.exit("no animations on the sign")
    print(f"{len(anims)} animation(s) on the sign via {target}; backups in {backup}")
    before = after = 0
    todo = []
    for a in anims:
        data = get(a["id"], a["bytes"])
        if len(data) != a["bytes"]:
            sys.exit(f"{a['id']}: got {len(data)} of {a['bytes']} bytes, stopping (nothing changed)")
        with open(os.path.join(backup, a["id"] + ".anm"), "wb") as f:
            f.write(data)
        w, h, delays, frames, ver = decode(data)
        if ver == 3.5:
            print(f"  {a['id']}: already in the latest format ({len(data) // 1024} KB)")
            continue
        new = encode_best(w, h, delays, frames)
        if len(new) >= len(data):
            print(f"  {a['id']}: no gain ({len(data) // 1024} KB), left alone")
            continue
        w2, h2, d2, f2, _ = decode(new)
        frames = anim_v3.apply_mask(frames, w, h)   # what the new file must decode to
        if (w2, h2, d2) != (w, h, delays) or any(not np.array_equal(x, y) for x, y in zip(frames, f2)):
            sys.exit(f"{a['id']}: re-encoded file does not match, stopping (nothing changed)")
        print(f"  {a['id']}: {len(data) // 1024} KB -> {len(new) // 1024} KB")
        before += len(data)
        after += len(new)
        todo.append((a["id"], new))
    if s:
        s.close()
    if not todo:
        return
    if dry:
        print(f"dry run: would save {(before - after) // 1024} KB")
        return
    for name, new in todo:
        res = ac.upload_to(target, name, new, ac.print_progress)
        if "error" in res:
            sys.exit(f"{name}: upload failed ({res['error']}); the old file is still on the sign, backup in {backup}")
    print(f"done: {before // 1024} KB -> {after // 1024} KB, {(before - after) // 1024} KB reclaimed; {res.get('free', 0) // 1024} KB free now")


if __name__ == "__main__":
    main()
