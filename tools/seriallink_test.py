#!/usr/bin/env python3
"""Test the "#ANIM" serial protocol without the board: builds the real
src/SerialLink.cpp + src/SetStore.cpp (+ Face/TextLayout for the text checks)
for the PC with the stand-ins in tools/seriallink_sim/, runs it with
stdin/stdout as the serial port, and drives it with the PC side the editor
uses (tools/sign_link.py, which uses anim_convert's serial helpers). Every
test runs on a clean link and again on a lossy one (bytes dropped both ways,
like the native USB socket now and then).

    python3 tools/seriallink_test.py          # WSL; needs the ArduinoJson copy
                                              # that `pio run -e s3` puts in .pio/libdeps
Exit code 1 on a failure.
"""
import json, os, random, shutil, subprocess, sys, tempfile, threading, time, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SIM = os.path.join(HERE, "seriallink_sim")
OUT = os.path.join(ROOT, ".pio", "sltest")
BIN = os.path.join(OUT, "sltest")
sys.path.insert(0, HERE)
import sign_link as sl   # noqa: E402


def build():
    aj = os.path.join(ROOT, ".pio", "libdeps", "s3", "ArduinoJson", "src")
    if not os.path.isdir(aj):
        sys.exit("ArduinoJson not found: run `pio run -e s3` once (it fetches the libraries)")
    os.makedirs(OUT, exist_ok=True)
    srcs = [os.path.join(ROOT, "src", f) for f in ("SerialLink.cpp", "SerialLink.h", "SetStore.cpp", "SetStore.h", "Face.cpp", "TextLayout.cpp")]
    srcs += [os.path.join(SIM, f) for f in os.listdir(SIM) if f.endswith((".h", ".cpp"))]
    if os.path.exists(BIN) and os.path.getmtime(BIN) >= max(os.path.getmtime(p) for p in srcs):
        return
    # SerialLink.cpp includes "StopLightApp.h" with quotes, which finds the file next to it
    # first: compile a copy that sits next to the stand-ins.
    for f in ("StopLightApp.h", "SnakeDisplay.h", "Mp3Player.h"):
        shutil.copy(os.path.join(SIM, f), OUT)
    shutil.copy(os.path.join(ROOT, "src", "SerialLink.cpp"), OUT)
    flags = ["-std=c++17", "-O1", "-g", "-Wall", "-Wno-unused-result", "-DARDUINOJSON_ENABLE_ARDUINO_STRING=1",
             "-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0", "-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0", "-DARDUINOJSON_ENABLE_PROGMEM=0",
             "-I" + SIM, "-I" + os.path.join(ROOT, "src"), "-I" + aj]
    print("building", os.path.relpath(BIN, ROOT), "...")
    subprocess.check_call(["g++"] + flags + [os.path.join(OUT, "SerialLink.cpp"), os.path.join(SIM, "sim_main.cpp"),
                                             os.path.join(ROOT, "src", "SetStore.cpp"), os.path.join(ROOT, "src", "Face.cpp"),
                                             os.path.join(ROOT, "src", "TextLayout.cpp"), "-o", BIN, "-lpthread"])


class SimLink:
    """The simulated board as a pyserial-like object (write, read, in_waiting).
    loss > 0 drops that share of the written chunks' bytes / of the board's
    output bytes, at random."""
    def __init__(self, root, loss=0.0, seed=1, env=None):
        e = dict(os.environ, SL_ROOT=root, **(env or {}))
        self.p = subprocess.Popen([BIN], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=open(os.path.join(root, "board.log"), "ab"), env=e)
        self.loss, self.rng = loss, random.Random(seed)
        self.buf, self.lock, self.timeout = bytearray(), threading.Lock(), 0.2
        self.dropped = 0
        threading.Thread(target=self._pump, daemon=True).start()

    def _pump(self):
        while True:
            b = self.p.stdout.read1(4096)
            if not b:
                return
            if self.loss:
                keep = bytearray()
                for ch in b:
                    if self.rng.random() < self.loss / 50:
                        self.dropped += 1
                    else:
                        keep.append(ch)
                b = bytes(keep)
            with self.lock:
                self.buf += b

    @property
    def in_waiting(self):
        with self.lock:
            return len(self.buf)

    def read(self, n=1):
        end = time.time() + self.timeout
        while True:
            with self.lock:
                if self.buf:
                    out = bytes(self.buf[:n]); del self.buf[:n]
                    return out
            if time.time() >= end:
                return b""
            time.sleep(0.002)

    def write(self, data):
        if self.loss and len(data) > 1 and self.rng.random() < self.loss:
            i = self.rng.randrange(len(data))
            data = data[:i] + data[i + 1:]   # one byte lost on the way
            self.dropped += 1
        self.p.stdin.write(data)
        self.p.stdin.flush()
        return len(data)

    def reset_input_buffer(self):
        with self.lock:
            self.buf.clear()

    def close(self):
        try:
            self.p.stdin.close()
        except Exception:
            pass
        self.p.wait(5)


FAILS = []


def check(name, cond, detail=""):
    print(("  ok    " if cond else "  FAIL  ") + name + (f"  ({detail})" if detail and not cond else ""))
    if not cond:
        FAILS.append(name)


def anm(frames=3, size=5000, seed=0):
    """A version 1 file the stand-in accepts (only the header is checked)."""
    rng = random.Random(seed)
    body = bytes(rng.randrange(256) for _ in range(size))
    return b"SAN\x01" + (128).to_bytes(2, "little") + (128).to_bytes(2, "little") + frames.to_bytes(2, "little") + b"\0\0" + body


def frame(t=("", "", ""), k=("short", "short", "short"), g=("", "", ""), **kw):
    f = {"t": list(t), "k": list(k), "g": list(g), "s": [0, 0, 0], "c": [48, 52, 12], "f": [0, 0, 0], "ms": 1000}
    f.update(kw)
    return f


def sample_sets():
    return [
        {"name": "Morning", "bpm": 128, "frames": [
            frame(("STOP", "המתן", "GO"), mp3=2),
            frame(("", "", ""), ("short", "off", "short"), ("dancing", "", "fx:quarters"), q=[[60, 12, 3], [1, 2, 3], [64, 0, 15]], hc=[30, 0, -45], hf=[0, 10, 0]),
            frame(("long text\nwith a break", "", ""), ("long", "short", "short"), ("", "u:clip-a", "fx:pulse"), gm=[0, 2, 0], af=[0, 1500, 0], al=[0, 2000, 0], ms=2500),
            frame(("A", "B", "C"), mp3=-2, fade=0.7, c=[64, 0, 63], f=[64, 64, 0]),
            frame(("", "", ""), mp3=-1, ms=50),
        ], "loops": [{"from": 2, "to": 3, "count": 4}, {"from": 3, "to": 4, "count": 2}, {"from": 5, "to": 5, "count": 2000}]},
        {"name": "שם ארוך מאוד שחורג מארבעים בתים בעברית", "bpm": 5, "frames": [frame(("Hi", "", ""), s=[24, 0, 0], ms=999999)]},
        {"name": "legacy", "t": ["OLD", "", ""], "m": 5},
    ]


def run(loss):
    tag = f"loss {loss:.0%}" if loss else "clean link"
    print(f"--- {tag}")
    root = tempfile.mkdtemp(prefix="sltest-")
    link = SimLink(root, loss, seed=7)
    b = sl.SerialBoard(link=link)
    try:
        conf = b.settings()
        check("CONF read", conf.get("volume") == 20 and conf.get("tracks") == 3 and conf.get("rotate180") is True, conf)
        b.put_settings({"volume": 7, "offDark": True, "rotate180": False}, conf)
        conf2 = b.settings()
        check("CONF write", conf2["volume"] == 7 and conf2["offDark"] is True and conf2["rotate180"] is False, conf2)

        first = b.sets()
        check("SETS on first boot = the example set", len(first) == 1 and first[0]["name"] == "Example", first)

        files = {"clip-a": anm(4, 9000, 1), "clip-b": anm(2, 30000, 2)}
        for aid, data in files.items():
            free = b.put_anim(aid, data)
            check(f"PUT {aid}", isinstance(free, int) and free > 0)
        info = b.info()
        check("LIST after PUT", sorted(a["id"] for a in info["anims"]) == ["clip-a", "clip-b"] and info["total"] == 0x4B0000, info)
        sums = b.sums(list(files))
        check("SUM = crc32 of the file", sums == {k: zlib.crc32(v) & 0xFFFFFFFF for k, v in files.items()}, sums)
        got = b.fetch_anim("clip-b", len(files["clip-b"]))
        check("GET clip-b back", got == files["clip-b"])

        sent = sample_sets()
        n = b.put_sets(sent)
        check("SPUT sample sets", n == 3, n)
        back = b.sets()
        check("SETS read back = canon_set(sent)", back == sl.canon_sets(sent),
              json.dumps([x for x in zip(back, sl.canon_sets(sent)) if x[0] != x[1]], ensure_ascii=False)[:600])
        check("canon_set is stable on the board's output", sl.canon_sets(back[:2]) == back[:2])   # not the legacy set: the firmware itself turns its mask into kinds on the next load
        check("stored_bytes = the board's JSON size", sl.stored_bytes(sent) == len(json.dumps({"sets": back}, ensure_ascii=False, separators=(",", ":")).encode()))

        def refused(sets, why):
            try:
                b.put_sets(sets)
                check(f"refuse: {why}", False, "accepted")
            except sl.BoardError as e:
                check(f"refuse: {why}", True)
                return str(e)
        e = refused([{"name": "x", "frames": [frame(("W" * 400, "", ""))]}], "text does not fit")
        check("  ... names set and frame", e and "set 1 (x), frame 1, top" in e, e)
        e = refused([{"name": "y", "frames": [frame(g=("", "", "u:nope"))]}], "missing animation")
        check("  ... names the animation", e and "nope" in e and "bottom" in e, e)
        refused([{"name": "z", "frames": [frame(g=("bogus", "", ""))]}], "unknown graphic")
        refused([{"name": str(i), "frames": [frame()]} for i in range(41)], "41 sets")
        refused([{"name": "long", "frames": [frame()] * 65}], "65 frames")
        refused([{"name": "none", "frames": []}], "no frames")
        big = [{"name": f"big{i}", "frames": [frame((f"T{k}", "W", "X"), ms=1000 + k) for k in range(40)]} for i in range(9)]
        e = refused(big, "more than 32 KB once stored")
        check("  ... says storage full", e and "storage full" in e, e)
        check("refused uploads changed nothing", b.sets() == back)

        check("DEL clip-b", b.del_anim("clip-b") > 0 and [a["id"] for a in b.info()["anims"]] == ["clip-a"])
        b.put_sets([])
        check("SPUT of no sets", b.sets() == [])
    except Exception as e:
        check(f"no exception ({type(e).__name__})", False, str(e))
    finally:
        link.close()
        log = open(os.path.join(root, "board.log"), errors="replace").read()
        check("board: no lock leak / unbalanced unlock", "LOCK LEAK" not in log and "UNLOCK WITHOUT" not in log and link.p.returncode == 0, log[-300:])
        if loss:
            print(f"  ({link.dropped} bytes dropped on the way)")
        shutil.rmtree(root, ignore_errors=True)


if __name__ == "__main__":
    build()
    run(0.0)
    run(0.03)
    print("FAILED: " + ", ".join(FAILS) if FAILS else "all passed")
    sys.exit(1 if FAILS else 0)
