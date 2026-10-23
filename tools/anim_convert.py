#!/usr/bin/env python3
"""Turn a GIF, a video file or a YouTube (or any yt-dlp) link into a StopLight
animation (.anm) and optionally upload it to the sign.

Run it with Windows Python (it has ffmpeg via imageio-ffmpeg, yt-dlp, numpy
and Pillow, and can reach the sign's WiFi through the USB dongle):

  python.exe tools/anim_convert.py clip.gif --name cat
  python.exe tools/anim_convert.py "https://youtu.be/..." --start 12 --duration 4 --shape full --upload
  python.exe tools/anim_convert.py dance.mp4 --fps 15 --dither --out dance.anm

--shape circle  128x128, shown inside one light (picked per light on the page)
--shape full    128x384, fills the whole sign
The file keeps the panels' full colour (2 bits per channel, 64 colours) plus a
silhouette bit per pixel (light/dark at --threshold), so on the page every
use can pick: original colours, silhouette in the light's colours, or the
silhouette inverted.

File format: see src/AnimStore.h.
"""
import urllib.parse
import argparse, json, os, re, struct, subprocess, sys, tempfile, urllib.request, uuid, zlib

import numpy as np

for _stream in (sys.stdout, sys.stderr):   # a Windows console (cp1252) cannot print emoji in video titles
    try:
        _stream.reconfigure(errors="replace")
    except Exception:
        pass

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # anim_v3 next to this file

W = 128
BAYER4 = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], dtype=np.float32) / 16.0 - 0.5


def ffmpeg_exe():
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except ImportError:
        return "ffmpeg"


def ffmpeg_dir():
    """A folder holding the ffmpeg binary under the name yt-dlp looks for."""
    exe = ffmpeg_exe()
    if os.path.basename(exe).lower() in ("ffmpeg", "ffmpeg.exe"):
        return os.path.dirname(exe) or None
    d = os.path.join(os.path.dirname(os.path.abspath(__file__)), "gfx", "bin")
    dst = os.path.join(d, "ffmpeg.exe" if os.name == "nt" else "ffmpeg")
    if not os.path.exists(dst) or os.path.getsize(dst) != os.path.getsize(exe):
        import shutil
        os.makedirs(d, exist_ok=True)
        shutil.copyfile(exe, dst)
        os.chmod(dst, 0o755)
    return d


DOWNLOAD_RES = 480   # the sign shows at most 128x384 pixels: 480p leaves room to crop and downloads far less than 720p


def download(url, tmp, max_seconds=60, progress=None, max_res=DOWNLOAD_RES):
    """Fetch a video with yt-dlp; only the first max_seconds (0 = all), at most
    max_res pixels on the shorter side (a portrait Short keeps 480 px across).
    progress(phase, done, total, unit) is called as bytes arrive (total 0 = unknown)."""
    import yt_dlp
    from yt_dlp.utils import download_range_func
    fdir = ffmpeg_dir()
    if fdir and fdir not in os.environ.get("PATH", ""):
        os.environ["PATH"] = fdir + os.pathsep + os.environ.get("PATH", "")   # yt-dlp's partial-download check only searches PATH
    opts = {"outtmpl": os.path.join(tmp, "src.%(ext)s"), "quiet": True, "no_warnings": True,
            "format": "bv*/b",   # video only when there is a choice: the sign has no use for sound
            # highest resolution up to max_res (by the shorter side; the smallest above it when there is none),
            # then H.264, which every ffmpeg decodes quickly
            "format_sort": [f"res:{int(max_res)}", "vcodec:h264", "ext:mp4"],
            "ffmpeg_location": fdir, "noplaylist": True}
    import threading, time
    last_hook = [0.0]
    if progress:
        def hook(d):
            if d.get("status") == "downloading":
                last_hook[0] = time.time()
                progress("download", d.get("downloaded_bytes") or 0, d.get("total_bytes") or d.get("total_bytes_estimate") or 0, "bytes")
        opts["progress_hooks"] = [hook]
        opts["noprogress"] = True
    stop = threading.Event()

    def watch():   # partial (ffmpeg) downloads send no progress events: follow the growing file instead
        while not stop.wait(0.4):
            if time.time() - last_hook[0] > 1.5:
                size = sum(os.path.getsize(os.path.join(tmp, f)) for f in os.listdir(tmp) if os.path.isfile(os.path.join(tmp, f)))
                if size:
                    progress("download", size, 0, "bytes")
    if progress:
        threading.Thread(target=watch, daemon=True).start()
    got = None
    try:
        with yt_dlp.YoutubeDL(opts) as y:
            info = y.extract_info(url, download=False)
            got = _fetch_start(y, info, tmp, max_seconds, progress) if max_seconds else None
            if not max_seconds:
                y.process_info(info)   # the whole video, in chunks (fast)
        if max_seconds and not got:
            # Fallback: yt-dlp cuts the range through ffmpeg, which reads one long stream that
            # YouTube paces at about twice playback speed (30 s for a minute, measured 2026-10-20).
            # No force_keyframes_at_cuts: that re-encodes the range; the cut starts at 0, a keyframe.
            opts["download_ranges"] = download_range_func(None, [(0, max_seconds)])
            with yt_dlp.YoutubeDL(opts) as y:
                info = y.extract_info(url, download=True)
    finally:
        stop.set()
    if progress:
        sys.stderr.write("\n") if progress is print_progress else None
    print(f"downloaded: {info.get('title', url)}")
    if got:
        return got
    files = [f for f in os.listdir(tmp) if f.startswith("src.")]
    if not files:
        sys.exit("download failed")
    return os.path.join(tmp, files[0])


def _fetch_start(y, info, tmp, max_seconds, progress=None):
    """Download just the start of the chosen format with plain range requests and
    cut the first max_seconds out of it by stream copy -> path of tmp/src.mp4, or
    None when the format cannot be fetched that way (then yt-dlp's ffmpeg path is used).
    YouTube serves ranges of a few MB at full speed (~10 MB/s here) but paces one long
    stream at about twice playback speed. The amount comes from the file's average
    bitrate with a margin; when the start does not hold max_seconds of video, more is
    fetched. Works for fragmented MP4 / WebM (YouTube's video-only formats); a file
    whose index sits at the end is only usable once complete."""
    from yt_dlp.networking import Request
    url, dur = info.get("url"), info.get("duration") or 0
    if not url or info.get("protocol") not in ("http", "https") or info.get("requested_formats"):
        return None
    size = info.get("filesize") or info.get("filesize_approx") or 0
    want = int(size * (max_seconds + 10) / dur * 1.5) + (512 << 10) if size and dur else 6 << 20
    headers = dict(info.get("http_headers") or {})
    part, out = os.path.join(tmp, "part." + (info.get("ext") or "mp4")), os.path.join(tmp, "src.mp4")
    got, total = 0, 0
    try:
        with open(part, "wb") as f:
            for _ in range(12):
                if total:
                    want = min(want, total)
                while got < want:
                    end = min(want, got + (8 << 20)) - 1   # yt-dlp's own chunks are 10 MB
                    r = y.urlopen(Request(url, headers={**headers, "Range": f"bytes={got}-{end}"}))
                    if r.status != 206:
                        r.close()
                        return None
                    m = re.search(r"/(\d+)$", r.headers.get("Content-Range", ""))
                    total = int(m.group(1)) if m else total
                    start = got
                    while True:
                        b = r.read(1 << 18)
                        if not b:
                            break
                        f.write(b)
                        got += len(b)
                        if progress:
                            progress("download", got, total and min(want, total) or want, "bytes")
                    r.close()
                    if got == start or (total and got >= total):
                        break
                f.flush()
                complete = bool(total) and got >= total
                have = _cut_start(part, out, max_seconds)
                if have >= max_seconds - 0.5 or complete:
                    if have <= 0:   # an odd container: hand over the file as it is
                        if not complete:
                            return None
                        f.close()
                        os.replace(part, out)
                    return out
                want = got + max(2 << 20, got // 2)
    except Exception as e:
        print(f"range download failed ({e}); using ffmpeg instead", file=sys.stderr)
        return None
    finally:
        if os.path.exists(part):
            os.remove(part)
    return None


def _cut_start(src, out, seconds):
    """Stream-copy the first seconds of src's video into out; -> seconds written (0 on failure)."""
    subprocess.run([ffmpeg_exe(), "-v", "error", "-y", "-i", src, "-map", "0:v:0", "-t", str(seconds), "-c", "copy", out],
                   capture_output=True)   # a truncated last fragment only costs a warning
    return probe_duration(out) if os.path.exists(out) and os.path.getsize(out) > 0 else 0


def probe_duration(src):
    """Seconds of video in src (0 if unknown), from ffmpeg's banner."""
    r = subprocess.run([ffmpeg_exe(), "-hide_banner", "-i", src], capture_output=True, text=True, errors="replace")
    m = re.search(r"Duration: (\d+):(\d+):([\d.]+)", r.stderr)
    return int(m.group(1)) * 3600 + int(m.group(2)) * 60 + float(m.group(3)) if m else 0


ROTATE = {0: "", 90: "transpose=1,", 180: "hflip,vflip,", 270: "transpose=2,"}   # clockwise degrees


def read_frames(src, h, fps, start, duration, fit, crop=None, progress=None, rotate=0):
    """Frames as uint8 arrays (h, W, 3), sampled at fps and scaled to W x h.
    crop = (x, y, w, h) as fractions of the source picture, applied first."""
    pre = ROTATE.get(int(rotate) % 360, "")   # rotation first: the crop is measured on the turned picture
    if crop:
        cx, cy, cw, ch = crop
        pre += f"crop=trunc(iw*{cw}/2)*2:trunc(ih*{ch}/2)*2:iw*{cx}:ih*{cy},"
    if fit == "fill":
        vf = f"scale={W}:{h}:force_original_aspect_ratio=increase:flags=area,crop={W}:{h}"
    else:
        vf = f"scale={W}:{h}:force_original_aspect_ratio=decrease:flags=area,pad={W}:{h}:(ow-iw)/2:(oh-ih)/2:black"
    cmd = [ffmpeg_exe(), "-v", "error"]
    if start:
        cmd += ["-ss", str(start)]
    if duration:
        cmd += ["-t", str(duration)]
    cmd += ["-i", src, "-vf", f"{pre}fps={fps},{vf}", "-an", "-f", "rawvideo", "-pix_fmt", "rgb24", "-"]
    size = W * h * 3
    expect = int((duration or max(0.0, probe_duration(src) - (start or 0))) * fps) if progress else 0
    with tempfile.TemporaryFile() as errf:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=errf)
        chunks = []
        while True:
            b = proc.stdout.read(size)
            if not b or len(b) < size:
                break
            chunks.append(b)
            if progress:
                progress("decode", len(chunks), max(expect, len(chunks)), "frames")
        proc.wait()
        errf.seek(0)
        err = errf.read().decode(errors="replace")
    if proc.returncode != 0:
        raise RuntimeError("ffmpeg failed: " + err[-400:])
    if not chunks:
        raise RuntimeError("no frames decoded (check the start time and length)")
    return np.frombuffer(b"".join(chunks), dtype=np.uint8).reshape(len(chunks), h, W, 3)


def quantise(frame, dither, threshold, gain):
    f = frame.astype(np.float32) * gain
    lum = f @ np.array([0.299, 0.587, 0.114], dtype=np.float32)
    if dither:
        f = f + np.tile(BAYER4, (frame.shape[0] // 4 + 1, W // 4))[: frame.shape[0], :W, None] * 85
    q = np.clip(np.rint(f / 85.0), 0, 3).astype(np.uint8)
    px = (q[..., 0] << 4) | (q[..., 1] << 2) | q[..., 2]
    px |= (lum >= threshold * 255).astype(np.uint8) << 6
    return px


def rle(px):
    flat = px.reshape(-1)
    change = np.flatnonzero(np.diff(flat)) + 1
    starts = np.concatenate(([0], change))
    lengths = np.diff(np.concatenate((starts, [flat.size])))
    out = bytearray()
    for s, n in zip(starts.tolist(), lengths.tolist()):
        v = int(flat[s])
        while n > 0:
            k = min(n, 255)
            out += bytes((k, v))
            n -= k
    return bytes(out)


def deflate(px):
    """Raw deflate, 1 KB window: the board inflates through a 1 KB ring buffer."""
    c = zlib.compressobj(9, zlib.DEFLATED, -10, 9)
    return c.compress(px.tobytes()) + c.flush()


def build(frames_px, delay_ms, compress=True, progress=None):
    """compress: True / 3 = version 3 (context model, default), 2 = per-frame deflate, False / 1 = run-length."""
    # merge identical consecutive frames (GIF holds, still video) into longer delays
    frames, delays = [], []
    for px in frames_px:
        if frames and np.array_equal(px, frames[-1]) and delays[-1] + delay_ms <= 60000:
            delays[-1] += delay_ms
        else:
            frames.append(px)
            delays.append(delay_ms)
    h = frames[0].shape[0]
    if compress is True or compress == 3:
        # Format 3 carries a 7.4 KB trained model, so a tiny clip can come out
        # larger than per-frame deflate: keep whichever is smaller.
        import anim_v3
        if progress:
            progress("encode", 0, 0, "")
        v3 = anim_v3.encode(W, h, delays, frames)
        v2, _ = build(anim_v3.apply_mask(frames_px, W, h), delay_ms, 2)   # same (masked) pixels either way
        return (v3 if len(v3) <= len(v2) else v2), len(frames)
    blobs = [deflate(p) if compress else rle(p) for p in frames]
    out = bytearray((b"SAN\x02" if compress else b"SAN\x01") + struct.pack("<HHHH", W, h, len(frames), 0))
    for d, b in zip(delays, blobs):
        out += struct.pack("<HI", min(d, 65535), len(b))
    for b in blobs:
        out += b
    return bytes(out), len(frames)


SIGN_USB_IDS = {0x303A: "native USB", 0x1A86: "UART (CH343)"}   # Espressif USB-Serial-JTAG, WCH CH343


def sign_ports():
    """Serial ports that look like the sign's ESP32-S3: [(port, description)]."""
    from serial.tools import list_ports
    return [(p.device, SIGN_USB_IDS[p.vid]) for p in list_ports.comports() if p.vid in SIGN_USB_IDS]


def serial_link(port):
    """Open the sign's serial port without resetting it (DTR/RTS low before open)."""
    import serial
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.2
    s.dtr = False
    s.rts = False
    s.open()
    return s


def serial_reply(s, timeout=8.0):
    """Next '#ANIM ...' line from the board (log lines are skipped), or None on timeout."""
    import time
    end = time.time() + timeout
    buf = getattr(s, "_anim_buf", b"")
    try:
        while True:
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                line = line.strip().decode(errors="replace")
                if line.startswith("#ANIM "):
                    return line[6:]
            if time.time() >= end:
                return None
            buf += s.read(max(1, s.in_waiting))   # returns as soon as anything arrives
    finally:
        s._anim_buf = buf


def upload_serial(port, name, data, progress=None):
    """Upload over the UART or native USB socket. Returns {"free": n, "port": p} or {"error": why}.
    Packets carry offset + CRC; on a lost packet or reply the board's position decides what is resent."""
    if port in ("", "usb", "auto"):
        ports = sign_ports()
        if not ports:
            return {"error": "no sign found on a USB port"}
        port = ports[0][0]
    try:
        s = serial_link(port)
    except Exception as e:
        return {"error": f"cannot open {port}: {e}"}
    try:
        s.reset_input_buffer()
        r = serial_put(s, f"PUT {name} {len(data)}", data, progress)
        return {"free": int(r.split()[0]), "port": port}
    except Exception as e:
        return {"error": str(e)}
    finally:
        s.close()


def serial_put(s, header, data, progress=None, end_timeout=15):
    """Send data over an open serial link: '#ANIM <header>' (PUT <name> <size> or
    SPUT <size>), DATA packets, END. Returns the text after the board's 'OK ' or
    raises RuntimeError(why). progress(done, total)."""
    import time, zlib
    def fail(r):
        raise RuntimeError(r[4:] if r and r.startswith("ERR ") else (r or "no answer from the sign (is the new firmware on it?)"))
    s.write(f"\n#ANIM {header}\n".encode())
    end = time.time() + 25   # the first write on a nearly full file system can take seconds
    r = serial_reply(s, 25)
    while r is not None and r != "READY" and not r.startswith("ERR") and time.time() < end:
        r = serial_reply(s, max(0.1, end - time.time()))   # a stale reply to an earlier command (LIST, GET...): skip it
    if r != "READY":
        fail(r)
    pos, misses = 0, 0
    while pos < len(data):
        chunk = data[pos:pos + 2048]
        s.write(f"\n#ANIM DATA {pos} {len(chunk)} {zlib.crc32(chunk) & 0xFFFFFFFF:08x}\n".encode() + chunk)
        r = serial_reply(s, 2.5)          # the board NAKs a stalled packet after 1 s
        while r and r.startswith("ACK ") and int(r[4:]) < pos + len(chunk) and int(r[4:]) <= pos:
            r = serial_reply(s, 2.5)      # a late ACK for an earlier packet: skip it
        if r is None:
            s.write(b"\n#ANIM POS\n")
            r = serial_reply(s, 2.5)
        if r and r.split(" ")[0] in ("ACK", "NAK", "POS"):
            new = int(r.split(" ")[1])
            misses = 0 if new > pos else misses + 1
            pos = new
        elif r and r.startswith("ERR"):
            fail(r)
        else:
            misses += 1
        if misses > 12:
            raise RuntimeError(f"the link keeps failing at byte {pos} of {len(data)}")
        if progress:
            progress(pos, len(data))
    s.write(f"\n#ANIM END {zlib.crc32(data) & 0xFFFFFFFF:08x}\n".encode())
    r = serial_reply(s, end_timeout)
    while r and r.split(" ")[0] in ("ACK", "NAK", "POS"):   # late replies to resent packets
        r = serial_reply(s, end_timeout)
    if r and r.startswith("OK "):
        return r[3:]
    fail(r)


def sign_list(target):
    """Animations on the sign: {"anims": [...], "free": bytes, "total": bytes} or
    {"error": why} (free/total missing from firmware older than 2026-10-20 on
    a cable). target as for upload_to."""
    if target.startswith("http"):
        try:
            j = json.loads(urllib.request.urlopen(target.rstrip("/") + "/api/anims", timeout=4).read())
            return {k: j[k] for k in ("anims", "free", "total") if k in j}
        except Exception as e:
            return {"error": f"cannot reach the sign at {target} ({e}). Join this PC to the StopLight WiFi first."}
    port = target
    if port in ("", "usb", "auto"):
        ports = sign_ports()
        if not ports:
            return {"error": "no sign found on a USB port"}
        port = ports[0][0]
    try:
        s = serial_link(port)
    except Exception as e:
        return {"error": f"cannot open {port}: {e}"}
    try:
        s.reset_input_buffer()
        for _ in range(3):   # the native USB link drops a reply now and then
            s.write(b"\n#ANIM LIST\n")
            r = serial_reply(s, 2)
            while r is not None and not r.startswith("LIST "):
                r = serial_reply(s, 2)   # a stale reply to an earlier command: skip it
            if r and r.startswith("LIST "):
                out = {"anims": json.loads(r[5:])}
                for _ in range(3):
                    s.write(b"\n#ANIM SPACE\n")
                    q = serial_reply(s, 1.5)
                    while q is not None and not q.startswith("SPACE "):
                        q = serial_reply(s, 1.5)
                    if q:
                        out["free"], out["total"] = (int(x) for x in q.split()[1:3])
                        break
                return out
        return {"error": "no answer from the sign"}
    finally:
        s.close()


def fetch_serial(s, name, size, progress=None, depth=3):
    """Read a stored animation back over an open serial link (#ANIM GET, blocks
    of 768 bytes checked by offset, length and CRC) with a few requests in
    flight. The native USB socket can hold the board's last reply back until
    the board sends something more, so a pause is answered with a POS that
    pushes it out, and blocks that still do not come are asked for again.
    progress(done, total)."""
    import base64, random, time, zlib
    # A first request whose offset no data block has (1..767) marks where this
    # file's replies start: a held-back block of an earlier read is dropped.
    last_err = None
    for _ in range(10):
        k, n = random.randrange(1, min(768, size - 8)), random.randrange(1, 8)
        s.write(f"\n#ANIM GET {name} {k} {n}\n".encode())
        end = time.time() + 1.5
        while time.time() < end:
            r = serial_reply(s, 0.15)
            if r is None:
                s.write(b"\n#ANIM POS\n")
            elif r.startswith(f"BLK {k} {n} "):
                break
            elif r.startswith("ERR"):
                last_err = r[4:]
        else:
            continue
        break
    else:
        raise RuntimeError(f"{name}: {last_err or 'no answer from the sign'}")
    want = {o: min(768, size - o) for o in range(0, size, 768)}
    got, todo, sent, have = {}, sorted(want), [], 0
    quiet, misses = time.time(), 0
    while len(got) < len(want):
        while todo and len(sent) < depth:
            o = todo.pop(0)
            s.write(f"\n#ANIM GET {name} {o} {want[o]}\n".encode())
            sent.append(o)
        r = serial_reply(s, 0.15)
        if r is None:
            if time.time() - quiet < 1.0:
                s.write(b"\n#ANIM POS\n")   # the last reply may be held back: this pushes it out
                continue
            misses += 1                      # damaged or lost: ask again
            if misses > 20:
                raise RuntimeError(f"{name}: read keeps failing at byte {min(sent + todo)}")
            todo[:0], sent, quiet = sent, [], time.time()
            continue
        try:
            _, o, ln, crc, b64 = r.split(" ", 4)
            o, data = int(o), base64.b64decode(b64)
            ok = r.startswith("BLK ") and o in want and o not in got and len(data) == int(ln) == want[o] \
                and zlib.crc32(data) & 0xFFFFFFFF == int(crc, 16)
        except ValueError:
            ok = False
        if not ok:
            continue
        got[o] = data
        have += len(data)
        quiet, misses = time.time(), 0
        if o in sent:   # replies come in request order: requests before this one lost theirs
            i = sent.index(o)
            todo[:0], sent = sent[:i], sent[i + 1:]
        elif o in todo:
            todo.remove(o)
        if progress:
            progress(have, size)
    return b"".join(got[o] for o in sorted(want))


def upload_to(target, name, data, progress=None):
    """target: an http:// URL (WiFi) or a COM port / 'usb' (cable).
    progress(phase, done, total, unit), phase "upload"."""
    if target.startswith("http"):
        return upload_http(target, name, data, progress)
    return upload_serial(target, name, data, (lambda d, t: progress("upload", d, t, "bytes")) if progress else None)


def upload_http(base, name, data, progress=None):
    """Upload over WiFi, streaming the body so progress(phase, done, total, unit) can follow it."""
    import http.client
    try:
        urllib.request.urlopen(base.rstrip("/") + "/api/anims", timeout=4).read()
    except Exception as e:
        return {"error": f"cannot reach the sign at {base} ({e}). Join this PC to the StopLight WiFi first."}
    boundary = uuid.uuid4().hex
    body = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"{name}.anm\"\r\n"
            f"Content-Type: application/octet-stream\r\n\r\n").encode() + data + f"\r\n--{boundary}--\r\n".encode()
    u = urllib.parse.urlparse(base)
    try:
        c = http.client.HTTPConnection(u.hostname, u.port or 80, timeout=120)
        c.putrequest("POST", "/api/anims?name=" + name + "&size=" + str(len(data)))
        c.putheader("Content-Type", f"multipart/form-data; boundary={boundary}")
        c.putheader("Content-Length", str(len(body)))
        c.endheaders()
        for i in range(0, len(body), 8192):
            c.send(body[i:i + 8192])
            if progress:
                progress("upload", min(i + 8192, len(body)), len(body), "bytes")
        r = c.getresponse()
        res = json.loads(r.read() or b"{}")
        c.close()
        if r.status != 200:
            return {"error": res.get("error", f"HTTP {r.status}")}
        return res
    except Exception as e:
        return {"error": str(e)}


def convert(src, shape="circle", fps=12, start=0, duration=0, fit="fill", crop=None, dither=False,
            threshold=0.5, gain=1.0, max_frames=150, progress=None, compress=True, rotate=0):
    """Library entry point (used by anim_studio.py): returns (anm bytes, frames, distinct)."""
    h = 384 if shape == "full" else 128
    frames = read_frames(src, h, fps, start, duration, fit, crop, progress, rotate)[:max_frames]
    px = []
    for i, f in enumerate(frames):
        px.append(quantise(f, dither, threshold, gain))
        if progress:
            progress("colours", i + 1, len(frames), "frames")
    data, n = build(px, max(20, int(round(1000 / fps))), compress, progress)
    return data, len(frames), n


def print_progress(phase, done, total, unit=""):
    if unit == "bytes":
        mb = lambda v: f"{v / 1048576:.1f} MB" if v >= 1048576 else f"{v / 1024:.0f} KB"
        txt = mb(done) + (f" of {mb(total)}" if total else " so far")
    else:
        txt = f"{done}" + (f" of {total}" if total else "") + (f" {unit}" if unit else "")
    pct = f" {100 * done // total:3d}%" if total else ""
    sys.stderr.write(f"\r{phase:8}{pct}  {txt}          ")
    sys.stderr.flush()
    if total and done >= total:
        sys.stderr.write("\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", help="GIF / video file, or a YouTube (yt-dlp) link")
    ap.add_argument("--download-seconds", type=float, default=60, help="links: download only the first N seconds (default 60, 0 = all)")
    ap.add_argument("--download-res", type=int, default=DOWNLOAD_RES, help=f"links: at most this many pixels on the shorter side (default {DOWNLOAD_RES})")
    ap.add_argument("--shape", choices=["circle", "full"], default="circle")
    ap.add_argument("--rotate", type=int, choices=[0, 90, 180, 270], default=0, help="turn the picture clockwise before cropping")
    ap.add_argument("--crop", help="x,y,w,h as fractions of the picture, e.g. 0.25,0,0.5,1 = the middle half")
    ap.add_argument("--fit", choices=["fill", "fit"], default="fill", help="fill = crop to the shape (default), fit = whole picture with black bars")
    ap.add_argument("--fps", type=float, default=12, help="frames per second (default 12)")
    ap.add_argument("--start", type=float, default=0, help="start at this many seconds")
    ap.add_argument("--duration", type=float, default=0, help="seconds to take (default: all, capped by --max-frames)")
    ap.add_argument("--max-frames", type=int, default=150)
    ap.add_argument("--dither", action="store_true", help="ordered dithering for smoother gradients (busier on the LEDs)")
    ap.add_argument("--threshold", type=float, default=0.5, help="silhouette: pixels brighter than this (0..1) count as light")
    ap.add_argument("--gain", type=float, default=1.0, help="brightness multiplier before quantising (dark videos: 1.5)")
    ap.add_argument("--format", type=int, choices=[1, 2, 3], default=3, help="file format: 3 = smallest (default), 2 = per-frame deflate, 1 = run-length (oldest firmware)")
    ap.add_argument("--name", help="name on the sign (a-z 0-9 - _, 24 chars); default from the file name")
    ap.add_argument("--out", help="write the .anm here (default <name>.anm next to this script's gfx folder)")
    ap.add_argument("--upload", nargs="?", const="http://192.168.4.1",
                    help="upload to the sign: http://192.168.4.1 (WiFi, the default), a COM port, or 'usb' (first sign found on a cable)")
    a = ap.parse_args()

    h = 384 if a.shape == "full" else 128
    with tempfile.TemporaryDirectory() as tmp:
        src = a.source
        if re.match(r"https?://", src):
            src = download(src, tmp, a.download_seconds, print_progress, a.download_res)
            base_name = a.name or "clip"
        else:
            if not os.path.exists(src):
                sys.exit(f"no such file: {src}")
            base_name = a.name or os.path.splitext(os.path.basename(src))[0]
        crop = tuple(float(v) for v in a.crop.split(",")) if a.crop else None
        try:
            frames = read_frames(src, h, a.fps, a.start, a.duration, a.fit, crop, print_progress, a.rotate)
        except RuntimeError as e:
            sys.exit(str(e))
        sys.stderr.write("\n")
    if len(frames) > a.max_frames:
        print(f"note: {len(frames)} frames, keeping the first {a.max_frames} (raise --max-frames or lower --fps)")
        frames = frames[: a.max_frames]
    name = re.sub(r"[^a-z0-9_-]+", "-", base_name.lower()).strip("-")[:24] or "anim"
    px = [quantise(f, a.dither, a.threshold, a.gain) for f in frames]
    data, n = build(px, max(20, int(round(1000 / a.fps))), a.format, print_progress)
    out = a.out or os.path.join(os.path.dirname(os.path.abspath(__file__)), "gfx", name + ".anm")
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, "wb") as fh:
        fh.write(data)
    secs = len(frames) / a.fps
    print(f"{name}: {a.shape} {W}x{h}, {len(frames)} frames ({n} distinct), {secs:.1f} s, {len(data) // 1024} KB -> {out}")
    if a.upload:
        res = upload_to(a.upload, name, data, print_progress)
        if "error" in res:
            sys.exit("upload failed: " + res["error"])
        print(f"uploaded as '{name}'" + (f" over {res['port']}" if res.get("port") else "") + f"; {res.get('free', 0) // 1024} KB left on the sign")


if __name__ == "__main__":
    main()
