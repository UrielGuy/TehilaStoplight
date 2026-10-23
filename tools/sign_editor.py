#!/usr/bin/env python3
"""StopLight offline editor: download everything from the sign once, edit the
sets, settings and animations on this PC (with a preview that plays the
sound from a local music folder), then upload the whole thing in one go.

    python.exe tools/sign_editor.py              # then open http://localhost:8095/
    python.exe tools/sign_editor.py --port 8097 --workspace D:\\sign

The page is the sign's own page (src/WebPage.h, read at every request so it
always matches the firmware), answered from a workspace folder instead of the
board, plus an editor card injected at serve time: target (WiFi or a USB
port), Download, Compare, Upload with a plan, the music folder, and imports
from the animation studio. Workspace (default tools/gfx/editor/, git-ignored):
    workspace.json   sets, settings, volume, music folder, target
    anims/<id>.anm   the animations
    board.json       what the sign had at the last download / upload (sizes, CRC32s,
                     sets, settings), so the editor can tell what changed
    incoming/        a download in progress (resumed per file)
    trash/<time>/    what a download or a delete replaced

Runs best under Windows Python (serial ports and the sign's WiFi are only
visible there). Texts are laid out by tools/stopcli, the same C++ as the
firmware: under Windows it runs in WSL through a persistent `wsl.exe` worker
(or tools/stopcli.exe if you build one); under Linux it is built and run
directly (WiFi targets only, no serial ports in WSL).
"""
import argparse, base64, email.parser, email.policy, json, mimetypes, os, queue, re, shutil, struct, subprocess, sys
import threading, time, traceback, urllib.parse, webbrowser, zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

ANIM_SPACE = 0x4B0000          # the LittleFS partition (partitions.csv)
MAX_TEXT_BYTES = 720
STUDIO_URL = "http://localhost:8090/"
FX = {"fx:quarters", "fx:pulse"}


# ======================================================================= renderer
CLI = os.path.join(HERE, "stopcli")
CLI_SRCS = ["tools/stopcli.cpp", "src/TextLayout.cpp", "src/Face.cpp", "src/Preview.cpp"]
CLI_DEPS = CLI_SRCS + ["src/fonts/StopFonts.h", "src/TextLayout.h", "src/Face.h", "src/Canvas.h", "src/Preview.h", "src/sprites/Sprites.h"]


def build_cli(out=CLI):
    """As tools/local_server.py: rebuild stopcli when its sources changed."""
    newest = max(os.path.getmtime(os.path.join(ROOT, p)) for p in CLI_DEPS if os.path.exists(os.path.join(ROOT, p)))
    if os.path.exists(out) and os.path.getmtime(out) >= newest:
        return
    print(f"building {out} ...", file=sys.stderr)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    tmp = out + ".tmp"
    subprocess.check_call(["g++", "-std=c++11", "-O2", "-Isrc"] + CLI_SRCS + ["-o", tmp], cwd=ROOT)
    os.replace(tmp, out)


def cli_works(path):
    try:
        return subprocess.run([path, "sizes"], capture_output=True, timeout=30).returncode == 0
    except Exception:
        return False


def render_worker():
    """--render-worker (runs in WSL): one JSON argument list per line on stdin,
    answered with '<length>\\n' + stopcli's output on stdout. A WSL distribution
    other than the one that built tools/stopcli may lack its glibc: it then
    builds its own copy under ~/.cache."""
    out = sys.stdout.buffer
    cli = CLI
    try:
        build_cli()
    except Exception:
        pass
    if not cli_works(cli):
        cli = os.path.expanduser("~/.cache/stoplight-editor/stopcli")
        try:
            build_cli(cli)
        except Exception as e:
            out.write(f"error: cannot build tools/stopcli in this WSL distribution ({e}); install g++ or pass --wsl-distro\n".encode())
            out.flush()
            return
    out.write(b"ready\n")
    out.flush()
    for line in sys.stdin.buffer:
        try:
            args = json.loads(line)
            r = subprocess.run([cli] + args, capture_output=True, timeout=30)
            data = r.stdout
            if not data:
                print(f"stopcli {args[:1]}: exit {r.returncode} {r.stderr[-300:]!r}", file=sys.stderr)
        except Exception as e:
            data = json.dumps({"ok": False, "error": f"engine failed: {e}"}).encode()
        out.write(b"%d\n" % len(data) + data)
        out.flush()


def wsl_path(p):
    p = os.path.abspath(p)
    m = re.match(r"^\\\\wsl(?:\.localhost|\$)\\[^\\]+(\\.*)$", p, re.I)
    if m:
        return m.group(1).replace("\\", "/")
    drive, rest = os.path.splitdrive(p)
    return "/mnt/" + drive[0].lower() + rest.replace("\\", "/")


class Renderer:
    """stopcli: a local binary (Linux, or tools/stopcli.exe on Windows) or a
    pool of persistent WSL workers (a wsl.exe call costs ~150 ms; a request to
    a running worker costs what stopcli costs)."""

    def __init__(self, distro=None, workers=2):
        self.distro, self.nworkers = distro, workers
        self.pool, self.started, self.start_lock = queue.Queue(), 0, threading.Lock()
        self._sizes = self._sprites = None
        self.error = ""
        exe = os.path.join(HERE, "stopcli.exe")
        if os.name != "nt":
            build_cli()
            self.mode, self.exe = "local", CLI
        elif os.path.exists(exe):
            self.mode, self.exe = "exe", exe
        else:
            self.mode, self.exe = "wsl", None

    def describe(self):
        return {"local": "tools/stopcli", "exe": "tools/stopcli.exe", "wsl": f"tools/stopcli in WSL ({self.started} worker(s))"}[self.mode]

    def _spawn(self):
        cmd = ["wsl.exe"] + (["-d", self.distro] if self.distro else []) + ["-e", "python3", wsl_path(__file__), "--render-worker"]
        p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        first = p.stdout.readline().strip()
        if first != b"ready":
            raise RuntimeError(first.decode(errors="replace") or "the WSL text engine did not start (is WSL installed, with python3 and g++?)")
        return p

    def _worker(self):
        try:
            return self.pool.get_nowait()
        except queue.Empty:
            pass
        with self.start_lock:   # one at a time: the first one may be compiling stopcli
            if self.started < self.nworkers:
                self.started += 1
                try:
                    return self._spawn()
                except Exception:
                    self.started -= 1
                    raise
        return self.pool.get(timeout=60)

    def run(self, args):
        args = [str(a) for a in args]
        if self.mode != "wsl":
            return subprocess.run([self.exe] + args, capture_output=True, timeout=30).stdout
        for attempt in range(2):
            p = self._worker()
            try:
                p.stdin.write(json.dumps(args).encode("utf-8") + b"\n")
                p.stdin.flush()
                n = int(p.stdout.readline())
                data = p.stdout.read(n)
                self.pool.put(p)
                return data
            except Exception:
                try:
                    p.kill()
                except Exception:
                    pass
                with self.start_lock:
                    self.started -= 1
                if attempt:
                    raise
        return b""

    def json(self, args, default):
        try:
            return json.loads(self.run(args) or b"null") or default
        except Exception as e:
            self.error = str(e)
            return default

    def safe_run(self, args):
        try:
            return self.run(args)
        except Exception as e:
            self.error = str(e)
            return b""

    def sizes(self):
        if not self._sizes:
            self._sizes = self.json(["sizes"], [])
        return self._sizes

    def sprites(self):
        if not self._sprites:
            self._sprites = self.json(["sprites"], [])
        return self._sprites

    def check(self, light, text, size=0):
        return self.json(["check", light, text, size], {"ok": False, "error": "engine failed"})


# ======================================================================= workspace
def anim_id(raw):
    """As AnimStore::sanitizeId."""
    raw = os.path.basename(str(raw).replace("\\", "/"))
    if raw.endswith(".anm"):
        raw = raw[:-4]
    out = ""
    for ch in raw.lower():
        if len(out) >= 24:
            break
        if ch.isascii() and (ch.isalnum() or ch in "-_"):
            out += ch
        elif ch in " .":
            out += "-"
    return out or "anim"


def anim_header(b):
    """{full, frames, ms, w, h} from an .anm file, or None if it is not one."""
    if len(b) < 12 or b[:3] != b"SAN" or b[3] not in (1, 2, 3):
        return None
    w, h, n, _ = struct.unpack_from("<HHHH", b, 4)
    t0 = 20 if b[3] == 3 else 12
    if len(b) < t0 + 6 * n:
        return None
    ms = sum(max(20, struct.unpack_from("<H", b, t0 + 6 * k)[0]) for k in range(n))
    return {"full": h > w, "frames": n, "ms": ms, "w": w, "h": h}


def crc32(b):
    return zlib.crc32(b) & 0xFFFFFFFF


def write_atomic(path, data):
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)


def stamp():
    return time.strftime("%Y%m%d-%H%M%S")


class Workspace:
    def __init__(self, path):
        self.dir = os.path.abspath(path)
        self.anims_dir = os.path.join(self.dir, "anims")
        self.incoming = os.path.join(self.dir, "incoming")
        self.trash = os.path.join(self.dir, "trash")
        os.makedirs(self.anims_dir, exist_ok=True)
        self.lock = threading.RLock()
        self._crc_cache = {}
        self.data = self._read("workspace.json") or {}
        self.data.setdefault("sets", [])
        self.data.setdefault("current", 0)
        self.data.setdefault("settings", {"offDark": False, "rotate180": True, "colourRotated": True})
        self.data.setdefault("volume", 20)
        self.data.setdefault("musicDir", "")
        self.data.setdefault("target", "http://192.168.4.1")

    def _read(self, name):
        p = os.path.join(self.dir, name)
        if not os.path.exists(p):
            return None
        with open(p, encoding="utf-8") as f:
            return json.load(f)

    def save(self):
        with self.lock:
            write_atomic(os.path.join(self.dir, "workspace.json"), json.dumps(self.data, ensure_ascii=False, indent=1).encode("utf-8"))

    def manifest(self):
        return self._read("board.json") or {}

    def save_manifest(self, m):
        with self.lock:
            write_atomic(os.path.join(self.dir, "board.json"), json.dumps(m, ensure_ascii=False, indent=1).encode("utf-8"))

    # ---- animations
    def anim_path(self, aid):
        return os.path.join(self.anims_dir, anim_id(aid) + ".anm")

    def anim_info(self):
        """id -> {id, full, frames, ms, bytes, crc} for every valid .anm in the workspace."""
        out = {}
        for fn in sorted(os.listdir(self.anims_dir)):
            if not fn.endswith(".anm"):
                continue
            p = os.path.join(self.anims_dir, fn)
            st = os.stat(p)
            key = (fn, st.st_size, st.st_mtime_ns)
            if key not in self._crc_cache:
                with open(p, "rb") as f:
                    b = f.read()
                h = anim_header(b)
                self._crc_cache[key] = None if h is None else dict(h, crc=crc32(b))
            h = self._crc_cache[key]
            if h:
                out[fn[:-4]] = {"id": fn[:-4], "full": h["full"], "frames": h["frames"], "ms": h["ms"], "bytes": st.st_size, "crc": h["crc"]}
        return out

    def anim_list(self):
        return [{k: a[k] for k in ("id", "full", "frames", "ms", "bytes")} for a in self.anim_info().values()]

    def space(self):
        """(free, total) as the sign would report them with the workspace's
        animations on it: the overhead the sign had at the last look is kept."""
        man = self.manifest()
        total = man.get("total") or ANIM_SPACE
        overhead = 0
        if man.get("free") is not None and man.get("anims"):
            overhead = max(0, total - man["free"] - sum((a["bytes"] + 4095) // 4096 * 4096 for a in man["anims"].values()))
        used = sum((a["bytes"] + 4095) // 4096 * 4096 for a in self.anim_info().values())
        return max(0, total - overhead - used), total

    def to_trash(self, path, sub=None):
        d = os.path.join(self.trash, sub or stamp())
        os.makedirs(d, exist_ok=True)
        shutil.move(path, os.path.join(d, os.path.basename(path)))
        return d

    # ---- music
    def tracks(self):
        d = self.data.get("musicDir") or ""
        if not d or not os.path.isdir(d):
            return []
        names = [f for f in os.listdir(d) if f.lower().endswith((".mp3", ".wav")) and os.path.isfile(os.path.join(d, f))]
        return sorted(names, key=lambda n: n.lower())


# ======================================================================= comparing
def board_look(board, log=None):
    """Everything the editor needs from the sign, in one go."""
    if log:
        log("reading the sign's animations")
    info = board.info()
    if log:
        log("reading the sign's sets")
    sets = board.sets()
    settings = board.settings()
    sums = {}
    if board.kind == "usb":
        if log:
            log("checksums of the sign's animations")
        sums = board.sums([a["id"] for a in info["anims"]])
    return {"anims": info["anims"], "free": info.get("free"), "total": info.get("total") or ANIM_SPACE, "sets": sets,
            "settings": settings, "sums": sums, "via": board.label, "kind": board.kind, "at": time.time()}


SETTING_KEYS = ("offDark", "rotate180", "colourRotated", "volume")


def ws_settings(ws):
    s = dict(ws.data["settings"])
    s["volume"] = ws.data["volume"]
    return {k: s.get(k) for k in SETTING_KEYS}


def sets_diff(mine, theirs):
    """Short description of how two set lists differ (by position)."""
    import sign_link as sl
    a, b = sl.canon_sets(mine), sl.canon_sets(theirs)
    out = {"changed": [], "added": [], "removed": []}
    for i in range(max(len(a), len(b))):
        nm = (a[i] if i < len(a) else b[i])["name"] or f"set {i + 1}"
        if i >= len(b):
            out["added"].append(nm)
        elif i >= len(a):
            out["removed"].append(nm)
        elif a[i] != b[i]:
            out["changed"].append(nm)
    out["same"] = not (out["changed"] or out["added"] or out["removed"])
    return out


def summary(ws):
    """Workspace vs the sign as last seen (no connection needed)."""
    man = ws.manifest()
    if not man.get("anims") and not man.get("sets") and not man.get("download"):
        return {"known": False}
    local = ws.anim_info()
    board = man.get("anims", {})
    out = {"known": True,
           "animsAdded": sorted(i for i in local if i not in board),
           "animsChanged": sorted(i for i in local if i in board and board[i].get("crc") != local[i]["crc"]),
           "animsOnlyOnSign": sorted(i for i in board if i not in local),
           "sets": sets_diff(ws.data["sets"], man.get("sets", [])),
           "settings": [k for k in SETTING_KEYS if k in man.get("settings", {}) and ws_settings(ws)[k] != man["settings"][k]]}
    out["differs"] = bool(out["animsAdded"] or out["animsChanged"] or out["animsOnlyOnSign"] or not out["sets"]["same"] or out["settings"])
    return out


def make_plan(ws, look, delete_removed):
    """The upload, step by step, with the space it needs. Order: animations the
    sign's current sets do not use are deleted first, then animations go up
    (replacements that shrink first), then the sets, then the remaining
    deletes, then the settings, so the sign never shows a set whose animation
    is missing."""
    import sign_link as sl
    cost = lambda n: (n + 4095) // 4096 * 4096
    local = ws.anim_info()
    board = {a["id"]: a for a in look["anims"]}
    man = ws.manifest().get("anims", {})
    sums = look.get("sums") or {}
    errors, warnings, steps = [], [], []

    def board_crc(aid):
        if sums.get(aid) is not None:
            return sums[aid]
        m = man.get(aid)
        return m.get("crc") if m and m.get("bytes") == board[aid]["bytes"] else None

    puts = []
    for aid, L in local.items():
        if aid not in board:
            puts.append({"op": "add", "id": aid, "bytes": L["bytes"], "old": 0, "note": "new"})
        else:
            bc = board_crc(aid)
            if bc is None:
                puts.append({"op": "replace", "id": aid, "bytes": L["bytes"], "old": board[aid]["bytes"], "note": "the sign's copy is not the one downloaded"})
            elif bc != L["crc"]:
                puts.append({"op": "replace", "id": aid, "bytes": L["bytes"], "old": board[aid]["bytes"], "note": "changed here"})
    only_sign = [aid for aid in board if aid not in local]
    dels = only_sign if delete_removed else []
    kept = [] if delete_removed else only_sign
    missing = [aid for aid in sl.anims_used(ws.data["sets"]) if aid not in local and aid not in kept]
    if missing:
        errors.append("the sets use animations that are neither here nor on the sign: " + ", ".join(missing))
    sets = ws.data["sets"]
    if len(sets) > sl.MAX_SETS:
        errors.append(f"{len(sets)} sets: the sign keeps at most {sl.MAX_SETS}")
    nbytes = sl.stored_bytes(sets)
    if nbytes > sl.MAX_JSON_BYTES:
        errors.append(f"the sets take {nbytes} bytes once stored: the sign keeps at most {sl.MAX_JSON_BYTES}")
    board_refs = set(sl.anims_used(look["sets"]))
    early = [d for d in dels if d not in board_refs]
    late = [d for d in dels if d in board_refs]
    free = look.get("free")
    if free is None:
        warnings.append("this sign's firmware does not report its free space: the plan cannot check that everything fits")
        free = 1 << 40
    for d in early:
        steps.append({"op": "delete", "id": d, "bytes": board[d]["bytes"], "note": "removed here"})
        free += cost(board[d]["bytes"])
    for p in sorted(puts, key=lambda p: (0 if p["op"] == "replace" and p["bytes"] <= p["old"] else 1, p["bytes"])):
        room = free + (cost(p["old"]) if p["old"] else 0)   # a replacement that does not fit deletes the old copy first
        if p["bytes"] + 8192 > room and late:
            for d in late:
                steps.append({"op": "delete", "id": d, "bytes": board[d]["bytes"], "note": "removed here (early, to make room: the sign's current sets lose it until the new sets arrive)"})
                free += cost(board[d]["bytes"])
                room += cost(board[d]["bytes"])
            late = []
        p["fits"] = p["bytes"] + 8192 <= room
        if not p["fits"]:
            errors.append(f"{p['id']} ({p['bytes'] // 1024} KB) does not fit: {max(0, room - 8192) // 1024} KB free at that point")
        steps.append(p)
        free = room - cost(p["bytes"]) if p["old"] else free - cost(p["bytes"])
    sd = sets_diff(sets, look["sets"])
    if not sd["same"]:
        parts = [f"{len(sd[k])} {w}" for k, w in (("changed", "changed"), ("added", "new"), ("removed", "removed")) if sd[k]]
        steps.append({"op": "sets", "count": len(sets), "bytes": nbytes, "note": ", ".join(parts), "detail": sd})
    for d in late:
        steps.append({"op": "delete", "id": d, "bytes": board[d]["bytes"], "note": "removed here (after the new sets)"})
        free += cost(board[d]["bytes"])
    mine, theirs = ws_settings(ws), look["settings"]
    ch = {k: mine[k] for k in ("offDark", "rotate180", "volume") if mine.get(k) is not None and mine[k] != theirs.get(k)}
    if ch:
        steps.append({"op": "settings", "changes": ch, "note": ", ".join(f"{k} {theirs.get(k)} -> {v}" for k, v in ch.items())})
    if mine.get("colourRotated") is not None and bool(mine["colourRotated"]) != bool(theirs.get("colourRotated")):
        steps.append({"op": "settings", "changes": {"colourRotated": mine["colourRotated"]}, "note": "colour wiring: the sign restarts"})
    # Has the sign changed since the editor last saw it (an edit from the phone)?
    m = ws.manifest()
    if m.get("sets") is not None and not sets_diff(look["sets"], m["sets"])["same"]:
        warnings.append("the sign's sets changed since the last download (edited from the phone?): uploading replaces them")
    changed_on_sign = [aid for aid in board if aid in man and man[aid].get("bytes") != board[aid]["bytes"]]
    if changed_on_sign:
        warnings.append("animations changed on the sign since the last download: " + ", ".join(changed_on_sign))
    return {"steps": steps, "errors": errors, "warnings": warnings, "ok": not errors, "nothing": not steps,
            "freeBefore": look.get("free"), "freeAfter": free if look.get("free") is not None else None, "total": look.get("total"),
            "onlyOnSign": only_sign, "deleteRemoved": delete_removed, "via": look.get("via"), "kind": look.get("kind")}


# ======================================================================= jobs
class Jobs:
    """One board operation at a time, run in a thread; the page polls it."""

    def __init__(self):
        self.lock = threading.Lock()
        self.cancel = False
        self.state = {"kind": None, "state": "idle"}

    def snapshot(self):
        with self.lock:
            return json.loads(json.dumps(self.state))

    def running(self):
        return self.state.get("state") == "running"

    def start(self, kind, fn):
        with self.lock:
            if self.state.get("state") == "running":
                return False
            self.cancel = False
            self.state = {"kind": kind, "state": "running", "phase": "connecting", "done": 0, "total": 0, "unit": "",
                          "log": [], "started": time.time(), "result": None, "error": None}
        def go():
            try:
                res = fn(self)
                with self.lock:
                    self.state.update(state="done", result=res, finished=time.time(), phase="done")
            except Exception as e:
                traceback.print_exc()
                with self.lock:
                    self.state.update(state="error", error=str(e), finished=time.time())
        threading.Thread(target=go, daemon=True).start()
        return True

    def phase(self, text, done=0, total=0, unit=""):
        with self.lock:
            self.state.update(phase=text, done=done, total=total, unit=unit)

    def progress(self, done, total, unit="bytes"):
        with self.lock:
            self.state.update(done=done, total=total, unit=unit)

    def log(self, msg):
        print("[editor]", msg, file=sys.stderr)
        with self.lock:
            self.state["log"] = (self.state.get("log") or [])[-40:] + [msg]
            self.state["phase"] = msg

    def check_cancel(self):
        if self.cancel:
            raise RuntimeError("stopped")


def retry(job, what, fn, tries=3):
    """WiFi drops and replugged cables: a step is tried again (each finished
    file is kept, so a retry or a later run only redoes what is missing)."""
    import sign_link as sl
    for i in range(tries):
        job.check_cancel()
        try:
            return fn()
        except sl.BoardError as e:
            msg = str(e)
            transient = any(t in msg for t in ("cannot reach", "broke off", "timed out", "no answer", "keeps failing", "cannot open", "got ", "Errno"))
            if not transient or i == tries - 1:
                raise sl.BoardError(f"{what}: {msg}")
            job.log(f"{what}: {msg}; trying again")
            time.sleep(2)


def do_download(ws, target, refetch, job):
    import sign_link as sl
    board = sl.open_board(target)
    try:
        look = board_look(board, job.log)
        os.makedirs(ws.incoming, exist_ok=True)
        local = ws.anim_info()
        man = ws.manifest().get("anims", {})
        got = {}
        anims = look["anims"]
        for k, a in enumerate(anims):
            job.check_cancel()
            aid, size = a["id"], a["bytes"]
            want = look["sums"].get(aid)
            dest = os.path.join(ws.incoming, aid + ".anm")
            if os.path.exists(dest) and os.path.getsize(dest) == size:
                with open(dest, "rb") as f:
                    data = f.read()
                if want is None or crc32(data) == want:
                    job.log(f"{k + 1}/{len(anims)} {aid}: already fetched")
                    got[aid] = data
                    continue
            L = local.get(aid)
            same = L and L["bytes"] == size and (L["crc"] == want if want is not None else man.get(aid, {}).get("crc") == L["crc"] and man[aid].get("bytes") == size)
            if same and not refetch:
                with open(ws.anim_path(aid), "rb") as f:
                    data = f.read()
                job.log(f"{k + 1}/{len(anims)} {aid}: unchanged, kept the workspace copy")
            else:
                job.phase(f"{k + 1}/{len(anims)} fetching {aid} ({size // 1024} KB)", 0, size, "bytes")
                data = retry(job, aid, lambda: board.fetch_anim(aid, size, lambda d, t: job.progress(d, t)))
                if want is not None and crc32(data) != want:
                    raise RuntimeError(f"{aid}: the copy read does not match the sign's checksum")
                job.log(f"{k + 1}/{len(anims)} {aid}: fetched {size // 1024} KB")
            write_atomic(dest, data)
            got[aid] = data
        job.check_cancel()
        # Everything is here: replace the workspace (what it had goes to the trash).
        with ws.lock:
            bin_ = stamp()
            if os.path.exists(os.path.join(ws.dir, "workspace.json")):
                os.makedirs(os.path.join(ws.trash, bin_), exist_ok=True)
                shutil.copy(os.path.join(ws.dir, "workspace.json"), os.path.join(ws.trash, bin_, "workspace.json"))
            for aid, L in ws.anim_info().items():
                if aid not in got or crc32(got[aid]) != L["crc"]:
                    ws.to_trash(ws.anim_path(aid), bin_)
            for aid in got:
                os.replace(os.path.join(ws.incoming, aid + ".anm"), ws.anim_path(aid))
            st = look["settings"]
            ws.data["sets"] = look["sets"]
            ws.data["current"] = 0
            ws.data["settings"] = {"offDark": bool(st.get("offDark")), "rotate180": bool(st.get("rotate180")), "colourRotated": bool(st.get("colourRotated"))}
            ws.data["volume"] = st.get("volume", ws.data["volume"])
            ws.data["target"] = target
            ws.save()
            old = ws.manifest()
            ws.save_manifest({"anims": {aid: dict(anim_header(d) or {}, bytes=len(d), crc=crc32(d)) for aid, d in got.items()},
                              "sets": look["sets"], "settings": {k: st.get(k) for k in SETTING_KEYS}, "free": look["free"], "total": look["total"],
                              "tracks": st.get("tracks"), "mp3": st.get("mp3"),
                              "download": {"at": time.time(), "via": look["via"]}, "upload": old.get("upload"), "check": {"at": time.time(), "via": look["via"]}})
            shutil.rmtree(ws.incoming, ignore_errors=True)
        return {"anims": len(got), "sets": len(look["sets"]), "trash": os.path.join(ws.trash, bin_)}
    finally:
        board.close()


LOOK = {}   # the last look at the sign, for re-planning without asking it again


def do_check(ws, target, delete_removed, job):
    import sign_link as sl
    board = sl.open_board(target)
    try:
        look = board_look(board, job.log)
    finally:
        board.close()
    LOOK.clear()
    LOOK.update(look, target=target)
    m = ws.manifest()
    m["check"] = {"at": time.time(), "via": look["via"]}
    m["tracks"] = look["settings"].get("tracks")
    ws.save_manifest(m)
    return {"plan": make_plan(ws, look, delete_removed)}


def do_upload(ws, target, delete_removed, job):
    import sign_link as sl
    board = sl.open_board(target)
    try:
        look = board_look(board, job.log)
        plan = make_plan(ws, look, delete_removed)
        if not plan["ok"]:
            raise RuntimeError("; ".join(plan["errors"]))
        n = len(plan["steps"])
        settings_now = dict(look["settings"])
        for k, st in enumerate(plan["steps"]):
            job.check_cancel()
            head = f"{k + 1}/{n}"
            m = ws.manifest()
            m.setdefault("anims", {})
            if st["op"] in ("add", "replace"):
                with open(ws.anim_path(st["id"]), "rb") as f:
                    data = f.read()
                job.phase(f"{head} uploading {st['id']} ({len(data) // 1024} KB)", 0, len(data), "bytes")
                free = retry(job, st["id"], lambda: board.put_anim(st["id"], data, lambda d, t: job.progress(d, t)))
                m["anims"][st["id"]] = dict(anim_header(data) or {}, bytes=len(data), crc=crc32(data))
                if free is not None:
                    m["free"] = free
                job.log(f"{head} {st['id']}: on the sign")
            elif st["op"] == "delete":
                job.phase(f"{head} deleting {st['id']}")
                free = retry(job, st["id"], lambda: board.del_anim(st["id"]))
                m["anims"].pop(st["id"], None)
                if free is not None:
                    m["free"] = free
                job.log(f"{head} {st['id']}: deleted")
            elif st["op"] == "sets":
                job.phase(f"{head} sending {st['count']} sets", 0, 1, "")
                retry(job, "sets", lambda: board.put_sets(ws.data["sets"], lambda d, t: job.progress(d, t, "")))
                m["sets"] = sl.canon_sets(ws.data["sets"])
                job.log(f"{head} sets: on the sign ({st['note']})")
            elif st["op"] == "settings":
                job.phase(f"{head} settings: {st['note']}")
                retry(job, "settings", lambda: board.put_settings(st["changes"], settings_now))
                settings_now.update(st["changes"])
                m.setdefault("settings", {}).update(st["changes"])
                job.log(f"{head} settings: {st['note']}")
            ws.save_manifest(m)
        m = ws.manifest()
        try:   # the sign's own numbers for the next plan
            info = board.info()
            m["free"], m["total"] = info.get("free"), info.get("total")
        except Exception:
            pass
        m["upload"] = {"at": time.time(), "via": board.label}
        ws.save_manifest(m)
        return {"steps": n}
    finally:
        board.close()


# ======================================================================= HTTP
def page_html():
    with open(os.path.join(ROOT, "src", "WebPage.h"), encoding="utf-8") as f:
        src = f.read()
    m = re.search(r'R"HTML\((.*)\)HTML"', src, re.S)
    html = m.group(1)
    with open(os.path.join(HERE, "sign_editor_inject.html"), encoding="utf-8") as f:
        inject = f.read()
    i = html.rfind("</body>")
    return (html[:i] + inject + html[i:]).encode("utf-8")


class H(BaseHTTPRequestHandler):
    ws = None        # Workspace
    R = None         # Renderer
    jobs = None      # Jobs

    def log_message(self, fmt, *args):
        if args and isinstance(args[0], str) and any(p in args[0] for p in ("/api/preview", "/api/state", "/api/editor/status", "/api/check", "/music/")):
            return
        super().log_message(fmt, *args)

    def _send(self, body, ctype, code=200, extra=None):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def _json(self, obj, code=200):
        self._send(json.dumps(obj, ensure_ascii=False).encode("utf-8"), "application/json; charset=utf-8", code)

    def _body(self):
        n = int(self.headers.get("Content-Length", 0) or 0)
        raw = self.rfile.read(n) if n else b""
        try:
            return json.loads(raw or b"{}")
        except ValueError:
            return {}

    # ---- the sign's API, answered from the workspace
    def _state(self):
        ws = self.ws
        tracks = ws.tracks()
        md = ws.data.get("musicDir") or ""
        free, total = ws.space()
        self._json({"current": ws.data["current"], "frame": 0, "live": False, "heap": 0, "heapMin": 0, "heapBlock": 0,
                    "sets": ws.data["sets"], "sizes": self.R.sizes(), "sprites": self.R.sprites(),
                    "mp3": {"present": bool(md and os.path.isdir(md)), "tracks": len(tracks), "volume": ws.data["volume"],
                            "error": "" if md and os.path.isdir(md) else ("music folder not found" if md else "")},
                    "anims": ws.anim_list(), "animFree": free, "animTotal": total,
                    "settings": dict(ws.data["settings"]), "editor": True})

    def _validate(self, body):
        """As WebUI::parseSet / checkLights, against the workspace."""
        frames = body.get("frames")
        if not isinstance(frames, list) or not frames:
            if isinstance(frames, list) or not body.get("t"):
                return "no frames"
            frames = [body]
        if len(frames) > 64:
            return "at most 64 frames"
        local = set(self.ws.anim_info())
        sprites = {s["id"] for s in self.R.sprites()} | FX
        names = ["top", "middle", "bottom"]
        for k, f in enumerate(frames):
            for i in range(3):
                def pick(key, d):
                    a = f.get(key)
                    return a[i] if isinstance(a, list) and i < len(a) else d
                if pick("k", "") == "off":
                    continue
                g = pick("g", "") or ""
                if g:
                    if g.startswith("u:"):
                        if g[2:] not in local:
                            return f"frame {k + 1}, {names[i]}: animation not in the workspace"
                    elif g not in sprites:
                        return f"frame {k + 1}, {names[i]}: unknown graphic"
                    continue
                t = str(pick("t", "") or "")
                if len(t.encode("utf-8")) > MAX_TEXT_BYTES:
                    return f"frame {k + 1}, {names[i]}: text too long"
                if not t.strip():
                    continue
                r = self.R.check(i, t, int(pick("s", 0) or 0))
                if not r.get("ok"):
                    return f"frame {k + 1}, {names[i]}: {r.get('error')}"
        return None

    def _store_set(self, body, replace=None):
        import sign_link as sl
        ws = self.ws
        sets = ws.data["sets"]
        if replace is None and len(sets) >= sl.MAX_SETS:
            return self._json({"error": "too many sets"}, 400)
        if replace is not None and not 0 <= replace < len(sets):
            return self._json({"error": "no such set"}, 404)
        err = self._validate(body)
        if err:
            return self._json({"error": err}, 400)
        new = list(sets)
        if replace is None:
            new.append(body)
        else:
            new[replace] = body
        if sl.stored_bytes(new) > sl.MAX_JSON_BYTES:
            return self._json({"error": "storage full: delete a set or shorten this one"}, 400)
        with ws.lock:
            ws.data["sets"] = new
            if replace is not None:
                ws.data["current"] = replace
            ws.save()
        self._state()

    def _music(self, n):
        tr = self.ws.tracks()
        if not 1 <= n <= len(tr):
            return self.send_error(404)
        p = os.path.join(self.ws.data["musicDir"], tr[n - 1])
        size = os.path.getsize(p)
        start, end = 0, size - 1
        rng = re.match(r"bytes=(\d*)-(\d*)", self.headers.get("Range", ""))
        if rng:
            if rng.group(1):
                start = int(rng.group(1))
            if rng.group(2):
                end = min(int(rng.group(2)), size - 1)
        with open(p, "rb") as f:
            f.seek(start)
            data = f.read(end - start + 1)
        self.send_response(206 if rng else 200)
        self.send_header("Content-Type", "audio/wav" if p.lower().endswith(".wav") else "audio/mpeg")
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(len(data)))
        if rng:
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.end_headers()
        self.wfile.write(data)

    def _status(self):
        import sign_link as sl
        ws = self.ws
        m = ws.manifest()
        tracks = ws.tracks()
        md = ws.data.get("musicDir") or ""
        self._json({"workspace": ws.dir, "target": ws.data.get("target"), "renderer": self.R.describe(), "rendererError": self.R.error,
                    "python": "windows" if os.name == "nt" else "linux", "studio": STUDIO_URL,
                    "last": {k: m.get(k) for k in ("download", "upload", "check")},
                    "boardTracks": m.get("tracks"), "summary": summary(ws),
                    "music": {"dir": md, "ok": bool(md and os.path.isdir(md)), "tracks": [{"n": i + 1, "name": t} for i, t in enumerate(tracks)]},
                    "job": self.jobs.snapshot(), "haveLook": bool(LOOK)})

    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        q = urllib.parse.parse_qs(u.query)
        ws = self.ws
        try:
            if u.path == "/":
                return self._send(page_html(), "text/html; charset=utf-8")
            if u.path == "/api/state":
                return self._state()
            if u.path == "/api/check":
                text = q.get("text", [""])[0]
                if len(text.encode("utf-8")) > MAX_TEXT_BYTES:
                    return self._json({"ok": False, "error": "too long"})
                if not text.strip():
                    return self._json({"ok": True, "size": 0, "lines": 0, "maxFit": 0})
                return self._json(self.R.check(int(q.get("light", ["0"])[0] or 0), text, int(q.get("size", ["0"])[0] or 0)))
            if u.path in ("/api/preview.rle", "/api/preview.bmp"):
                args = [f"{k}={v[0]}" for k, v in q.items() if k != "_"] + [f"o={1 if ws.data['settings'].get('offDark') else 0}"]
                rle = u.path.endswith(".rle")
                return self._send(self.R.safe_run(["rle" if rle else "preview"] + args), "application/octet-stream" if rle else "image/bmp")
            if u.path == "/api/anims":
                free, total = ws.space()
                return self._json({"anims": ws.anim_list(), "free": free, "total": total})
            if u.path.startswith("/api/anims/"):
                p = ws.anim_path(u.path.rsplit("/", 1)[1])
                if not os.path.exists(p):
                    return self.send_error(404)
                with open(p, "rb") as f:
                    return self._send(f.read(), "application/octet-stream")
            if u.path.startswith("/music/"):
                return self._music(int(re.sub(r"\D", "", u.path[7:]) or 0))
            if u.path == "/api/editor/status":
                return self._status()
            if u.path == "/api/editor/ports":
                import sign_link as sl
                return self._json({"ports": [{"port": p, "kind": k} for p, k in sl.sign_ports()]})
            if u.path == "/api/editor/importable":
                out = []
                for d, kind in ((os.path.join(HERE, "gfx"), "named"), (os.path.join(HERE, "gfx", "work"), "converted")):
                    if not os.path.isdir(d):
                        continue
                    for fn in os.listdir(d):
                        p = os.path.join(d, fn)
                        if fn.endswith(".anm") and os.path.isfile(p):
                            out.append({"path": p, "name": fn[:-4], "bytes": os.path.getsize(p), "kind": kind, "mtime": os.path.getmtime(p)})
                out.sort(key=lambda x: -x["mtime"])
                return self._json({"files": out[:60]})
            return self.send_error(404)
        except BrokenPipeError:
            pass

    def do_PUT(self):
        u = urllib.parse.urlparse(self.path)
        if u.path.startswith("/api/sets/"):
            return self._store_set(self._body(), replace=int(u.path.rsplit("/", 1)[1]))
        self.send_error(404)

    def do_DELETE(self):
        u = urllib.parse.urlparse(self.path)
        ws = self.ws
        if u.path == "/api/live":
            return self._state()
        if u.path.startswith("/api/anims/"):
            p = ws.anim_path(u.path.rsplit("/", 1)[1])
            if os.path.exists(p):
                ws.to_trash(p)
            free, total = ws.space()
            return self._json({"anims": ws.anim_list(), "free": free, "total": total})
        if u.path.startswith("/api/sets/"):
            i = int(u.path.rsplit("/", 1)[1])
            with ws.lock:
                if 0 <= i < len(ws.data["sets"]):
                    del ws.data["sets"][i]
                    if ws.data["current"] >= len(ws.data["sets"]):
                        ws.data["current"] = 0
                    ws.save()
            return self._state()
        self.send_error(404)

    def do_POST(self):
        u = urllib.parse.urlparse(self.path)
        ws = self.ws
        p = u.path
        if p == "/api/sets":
            return self._store_set(self._body())
        if p.startswith("/api/show/"):
            i = int(p.rsplit("/", 1)[1])
            if 0 <= i < len(ws.data["sets"]):
                ws.data["current"] = i
                ws.save()
            return self._state()
        if p == "/api/live":
            self._body()
            return self._state()
        if p == "/api/settings":
            b = self._body()
            with ws.lock:
                for k in ("offDark", "rotate180", "colourRotated"):
                    if isinstance(b.get(k), bool):
                        ws.data["settings"][k] = b[k]
                ws.save()
            return self._state()
        if p.startswith("/api/mp3/"):
            if p == "/api/mp3/volume":
                v = self._body().get("volume", 20)
                ws.data["volume"] = max(0, min(30, int(v) if isinstance(v, (int, float)) else 20))
                ws.save()
            return self._state()   # play / stop / rescan: the page plays the music itself
        if p.startswith("/api/anims/") and p.endswith("/rename"):
            src = anim_id(p[len("/api/anims/"):-len("/rename")])
            dst = anim_id(self._body().get("name", ""))
            if not os.path.exists(ws.anim_path(src)):
                return self._json({"error": "no such animation"}, 400)
            if dst != src:
                if os.path.exists(ws.anim_path(dst)):
                    return self._json({"error": f"there is already an animation called {dst}"}, 400)
                with ws.lock:
                    os.rename(ws.anim_path(src), ws.anim_path(dst))
                    for st in ws.data["sets"]:
                        for f in st.get("frames", []):
                            g = f.get("g") or []
                            for i, x in enumerate(g):
                                if x == "u:" + src:
                                    g[i] = "u:" + dst
                    ws.save()
            free, total = ws.space()
            return self._json({"anims": ws.anim_list(), "free": free, "total": total})
        if p == "/api/anims":   # multipart, as the page and anim_convert.upload_http send it
            raw = self.rfile.read(int(self.headers.get("Content-Length", 0) or 0))
            msg = email.parser.BytesParser(policy=email.policy.default).parsebytes(
                b"Content-Type: " + self.headers.get("Content-Type", "").encode() + b"\r\n\r\n" + raw)
            part = next((x for x in msg.iter_parts() if x.get_filename()), None) if msg.is_multipart() else None
            data = part.get_payload(decode=True) if part else b""
            q = urllib.parse.parse_qs(u.query)
            name = anim_id((q.get("name") or [""])[0] or (part.get_filename() if part else ""))
            if anim_header(data) is None:
                return self._json({"error": "not an animation file (make it with tools/anim_convert.py)"}, 400)
            write_atomic(ws.anim_path(name), data)
            free, total = ws.space()
            return self._json({"anims": ws.anim_list(), "free": free, "total": total})
        # ---- the editor's own calls
        if p.startswith("/api/editor/"):
            return self._editor(p[len("/api/editor/"):], self._body())
        self.send_error(404)

    def _editor(self, what, b):
        ws, jobs = self.ws, self.jobs
        target = (b.get("target") or ws.data.get("target") or "http://192.168.4.1").strip()
        if what in ("download", "check", "upload"):
            if jobs.running():
                return self._json({"error": "busy with another operation"}, 409)
            if b.get("target"):
                ws.data["target"] = target
                ws.save()
            dr = bool(b.get("deleteRemoved"))
            fn = {"download": lambda j: do_download(ws, target, bool(b.get("refetch")), j),
                  "check": lambda j: do_check(ws, target, dr, j),
                  "upload": lambda j: do_upload(ws, target, dr, j)}[what]
            jobs.start(what, fn)
            return self._json({"ok": True})
        if what == "plan":   # again from the last look, e.g. after ticking "delete removed"
            if not LOOK:
                return self._json({"error": "compare with the sign first"}, 400)
            return self._json({"plan": make_plan(ws, LOOK, bool(b.get("deleteRemoved")))})
        if what == "cancel":
            jobs.cancel = True
            return self._json({"ok": True})
        if what == "target":
            ws.data["target"] = target
            ws.save()
            return self._json({"ok": True})
        if what == "music":
            d = (b.get("dir") or "").strip().strip('"')
            if d and not os.path.isdir(d):
                return self._json({"error": f"no such folder: {d}"}, 400)
            ws.data["musicDir"] = d
            ws.save()
            return self._json({"ok": True, "tracks": len(ws.tracks())})
        if what == "browse":
            try:
                import tkinter
                from tkinter import filedialog
                root = tkinter.Tk()
                root.withdraw()
                root.attributes("-topmost", True)
                d = filedialog.askdirectory(title="The folder with the SD card's music", initialdir=ws.data.get("musicDir") or None)
                root.destroy()
            except Exception as e:
                return self._json({"error": f"no folder dialog here ({e}): type the path"}, 400)
            return self._json({"dir": os.path.normpath(d) if d else ""})
        if what == "import":
            src = b.get("path") or ""
            ok_dirs = [os.path.join(HERE, "gfx"), os.path.join(HERE, "gfx", "work")]
            if os.path.dirname(os.path.abspath(src)) not in [os.path.abspath(x) for x in ok_dirs] or not os.path.isfile(src):
                return self._json({"error": "not a file from the studio's folders"}, 400)
            with open(src, "rb") as f:
                data = f.read()
            if anim_header(data) is None:
                return self._json({"error": "not an animation file"}, 400)
            name = anim_id(b.get("name") or os.path.basename(src))
            write_atomic(ws.anim_path(name), data)
            return self._json({"ok": True, "id": name})
        return self._json({"error": "unknown editor call"}, 404)


def main():
    if "--render-worker" in sys.argv:
        return render_worker()
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=8095)
    ap.add_argument("--workspace", default=os.path.join(HERE, "gfx", "editor"))
    ap.add_argument("--wsl-distro", default=None, help="WSL distribution with g++ and python3 (default: the default one)")
    ap.add_argument("--no-browser", action="store_true")
    ap.add_argument("--system-font", action="store_true", help="draw preview text in a system font (no WSL needed; sizes and line breaks only approximate)")
    a = ap.parse_args()
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(errors="replace")
        except Exception:
            pass
    H.ws = Workspace(a.workspace)
    H.R = Renderer(a.wsl_distro)
    if a.system_font or (H.R.mode == "wsl" and not H.R.sizes()):
        # Windows without WSL (and no tools/stopcli.exe): approximate text in a system font
        try:
            import sysfont_render
            H.R = sysfont_render.SysFontRenderer()
        except ImportError:
            print("text previews need WSL, tools/stopcli.exe or Pillow (python -m pip install pillow)")
    H.jobs = Jobs()
    print(f"StopLight offline editor: http://localhost:{a.port}/   (workspace {H.ws.dir}; text engine: {H.R.describe()})")
    srv = ThreadingHTTPServer(("127.0.0.1", a.port), H)
    srv.daemon_threads = True
    if not a.no_browser:
        threading.Timer(0.8, lambda: webbrowser.open(f"http://localhost:{a.port}/")).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
