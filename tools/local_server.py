#!/usr/bin/env python3
"""Run the StopLight web page locally, against the real text engine, without the board.

    python3 tools/local_server.py          # then open http://localhost:8080/

Serves the same page the ESP32 serves (src/WebPage.h) and the same API, with
/api/check and /api/preview.bmp answered by tools/stopcli (built from the same
C++ as the firmware). Sets live in tools/local_sets.json instead of NVS.
"""
import json, os, re, subprocess, sys, urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLI = os.path.join(ROOT, "tools", "stopcli")
STORE = os.path.join(ROOT, "tools", "local_sets.json")
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
MAX_TEXT_BYTES = 720

def build_cli():
    srcs = ["tools/stopcli.cpp", "src/TextLayout.cpp", "src/Face.cpp", "src/Preview.cpp"]
    newest = max(os.path.getmtime(os.path.join(ROOT, p)) for p in srcs + ["src/fonts/StopFonts.h", "src/TextLayout.h", "src/Face.h"])
    if os.path.exists(CLI) and os.path.getmtime(CLI) >= newest:
        return
    print("building tools/stopcli ...")
    subprocess.check_call(["g++", "-std=c++11", "-O2", "-Isrc"] + srcs + ["-o", CLI], cwd=ROOT)

def page():
    src = open(os.path.join(ROOT, "src", "WebPage.h"), encoding="utf-8").read()
    m = re.search(r'R"HTML\((.*)\)HTML"', src, re.S)
    return m.group(1).encode("utf-8")

def load():
    if os.path.exists(STORE):
        return json.load(open(STORE, encoding="utf-8"))
    return {"sets": [{"name": "Example", "frames": [{"t": ["STOP", "WAIT", "GO"], "k": ["short", "short", "short"], "g": ["", "", ""], "s": [0, 0, 0], "c": [48, 52, 12], "f": [0, 0, 0], "m": 7, "ms": 10000}], "loop": {"count": 1}}]}

def save(state):
    json.dump(state, open(STORE, "w", encoding="utf-8"), ensure_ascii=False, indent=1)

state = load()
state.setdefault("current", 0)

def check(light, text, max_size=0):
    out = subprocess.run([CLI, "check", str(light), text, str(max_size)], capture_output=True, text=True).stdout
    return json.loads(out or '{"ok":false,"error":"engine failed"}')

def sizes():
    return json.loads(subprocess.run([CLI, "sizes"], capture_output=True, text=True).stdout or "[]")

def sprites():
    return json.loads(subprocess.run([CLI, "sprites"], capture_output=True, text=True).stdout or "[]")

class H(BaseHTTPRequestHandler):
    def _json(self, obj, code=200):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _state(self):
        self._json({"current": state["current"], "live": state.get("live", False), "sets": state["sets"], "sizes": sizes(), "sprites": sprites(),
                    "mp3": {"present": True, "tracks": 5, "volume": state.get("volume", 20), "error": ""}})   # a pretend module with five tracks

    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        q = urllib.parse.parse_qs(u.query)
        if u.path == "/":
            body = page()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif u.path == "/api/state":
            self._state()
        elif u.path == "/api/check":
            text = q.get("text", [""])[0]
            if len(text.encode("utf-8")) > MAX_TEXT_BYTES:
                self._json({"ok": False, "error": "too long"})
            else:
                self._json(check(int(q.get("light", ["0"])[0]), text, int(q.get("size", ["0"])[0] or 0)))
        elif u.path in ("/api/preview.bmp", "/api/preview.rle"):
            args = [f"{k}={v[0]}" for k, v in q.items() if k != "_"]
            cmd = "rle" if u.path.endswith(".rle") else "preview"
            bmp = subprocess.run([CLI, cmd] + args, capture_output=True).stdout
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream" if cmd == "rle" else "image/bmp")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(bmp)))
            self.end_headers()
            self.wfile.write(bmp)
        else:
            self.send_error(404)

    def _body(self):
        n = int(self.headers.get("Content-Length", 0))
        return json.loads(self.rfile.read(n) or b"{}")

    def _validate(self, body):
        """Mirror the firmware: every non-graphic, non-empty text must fit."""
        def check_lights(o, prefix=""):
            for i, name in enumerate(["top", "middle", "bottom"]):
                if (o.get("k") or ["", "", ""])[i] == "off":
                    continue
                g = (o.get("g") or ["", "", ""])[i]
                if g:
                    if g not in {s["id"] for s in sprites()}:
                        return f"{prefix}{name}: unknown graphic"
                    continue
                t = str((o.get("t") or ["", "", ""])[i]).strip()
                if not t:
                    continue
                r = check(i, t, int((o.get("s") or [0, 0, 0])[i] or 0))
                if not r["ok"]:
                    return f"{prefix}{name}: {r['error']}"
            return None
        if not body.get("frames"):
            return "no frames"
        if len(body["frames"]) > 64:
            return "at most 64 frames"
        n = len(body["frames"])
        for L in body.get("loops") or []:
            if not (1 <= L.get("from", 0) <= L.get("to", 0) <= n):
                return "loop outside the set"
        for k, f in enumerate(body["frames"]):
            e = check_lights(f, f"frame {k + 1}, ")
            if e:
                return e
        return None

    def _store_set(self, body, replace=None):
        err = self._validate(body)
        if err:
            return self._json({"error": err}, 400)
        if replace is None:
            state["sets"].append(body)
        elif 0 <= replace < len(state["sets"]):
            state["sets"][replace] = body
        state["live"] = False
        save(state)
        self._state()

    def do_PUT(self):
        u = urllib.parse.urlparse(self.path)
        if u.path.startswith("/api/sets/"):
            return self._store_set(self._body(), replace=int(u.path.rsplit("/", 1)[1]))
        self.send_error(404)

    def do_POST(self):
        u = urllib.parse.urlparse(self.path)
        if u.path == "/api/sets":
            self._store_set(self._body())
        elif u.path.startswith("/api/show/"):
            i = int(u.path.rsplit("/", 1)[1])
            if 0 <= i < len(state["sets"]):
                state["current"] = i
                print(f"[display] now showing set {i + 1}")
            self._state()
        elif u.path.startswith("/api/mp3/"):
            if u.path == "/api/mp3/volume":
                state["volume"] = int(self._body().get("volume", 20)); save(state)
            print(f"[mp3] {u.path}")
            self._state()
        elif u.path == "/api/live":
            b = self._body()
            state["live"] = True
            print(f"[display] LIVE draft: {b.get('t')} g={b.get('g')} m={b.get('m')}")
            self._state()
        else:
            self.send_error(404)

    def do_DELETE(self):
        u = urllib.parse.urlparse(self.path)
        if u.path == "/api/live":
            state["live"] = False
            print("[display] live off, back to stored sets")
            return self._state()
        if u.path.startswith("/api/sets/"):
            i = int(u.path.rsplit("/", 1)[1])
            if 0 <= i < len(state["sets"]):
                del state["sets"][i]
                if state["current"] >= len(state["sets"]):
                    state["current"] = 0
                save(state)
            self._state()
        else:
            self.send_error(404)

    def log_message(self, fmt, *args):
        if "/api/preview" not in str(args[0] if args else "") and "/api/state" not in str(args[0] if args else ""):
            super().log_message(fmt, *args)

if __name__ == "__main__":
    build_cli()
    print(f"StopLight local harness: http://localhost:{PORT}/   (sets in {STORE})")
    ThreadingHTTPServer(("0.0.0.0", PORT), H).serve_forever()
