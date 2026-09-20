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
    srcs = ["tools/stopcli.cpp", "src/TextLayout.cpp", "src/Face.cpp", "src/Preview.cpp", "src/Animation.cpp"]
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
    return {"interval": 60, "sets": [{"t": ["STOP", "WAIT", "GO"]}]}

def save(state):
    json.dump(state, open(STORE, "w", encoding="utf-8"), ensure_ascii=False, indent=1)

state = load()
state.setdefault("current", 0)

def check(light, text, max_size=0):
    out = subprocess.run([CLI, "check", str(light), text, str(max_size)], capture_output=True, text=True).stdout
    return json.loads(out or '{"ok":false,"error":"engine failed"}')

def sizes():
    return json.loads(subprocess.run([CLI, "sizes"], capture_output=True, text=True).stdout or "[]")

def anims():
    return json.loads(subprocess.run([CLI, "anims"], capture_output=True, text=True).stdout or "[]")

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
        self._json({"interval": state["interval"], "current": state["current"], "live": state.get("live", False), "sets": state["sets"], "sizes": sizes(), "animations": anims(), "sprites": sprites()})

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

    def do_POST(self):
        u = urllib.parse.urlparse(self.path)
        if u.path == "/api/sets":
            body = self._body()
            t = [str(x).strip() for x in body.get("t", ["", "", ""])][:3] + [""] * 3
            sz = [int(x or 0) for x in body.get("s", [0, 0, 0])][:3] + [0] * 3
            gsel = body.get("g", ["", "", ""])
            for i, name in enumerate(["top", "middle", "bottom"]):
                if i < len(gsel) and gsel[i]:
                    continue
                r = check(i, t[i], sz[i])
                if not r["ok"]:
                    return self._json({"error": f"{name}: {r['error']}"}, 400)
            ids = {a["id"] for a in anims()}
            a = body.get("a", "all"); a = a if a in ids else "all"
            p = max(100, min(60000, int(body.get("p", 1000) or 1000)))
            defaults = [48, 52, 12]
            c = [int(x) & 63 if x not in (None, "") else defaults[i] for i, x in enumerate((body.get("c", defaults) + defaults)[:3])]
            f = [int(x or 0) & 63 for x in (body.get("f", [0, 0, 0]) + [0, 0, 0])[:3]]
            valid = {s["id"] for s in sprites()}
            g = [x if x in valid else "" for x in (body.get("g", ["", "", ""]) + ["", "", ""])[:3]]
            state["sets"].append({"t": t[:3], "s": sz[:3], "c": c, "f": f, "g": g, "a": a, "p": p})
            save(state)
            self._state()
        elif u.path.startswith("/api/show/"):
            i = int(u.path.rsplit("/", 1)[1])
            if 0 <= i < len(state["sets"]):
                state["current"] = i
                print(f"[display] now showing set {i + 1}: {state['sets'][i]['t']}")
            self._state()
        elif u.path == "/api/live":
            b = self._body()
            state["live"] = True
            print(f"[display] LIVE draft: {b.get('t')} g={b.get('g')} a={b.get('a')}")
            self._state()
        elif u.path == "/api/interval":
            state["interval"] = max(5, min(86400, int(self._body().get("seconds", 60))))
            save(state)
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
        if "/api/preview" not in (args[0] if args else "") and "/api/state" not in (args[0] if args else ""):
            super().log_message(fmt, *args)

if __name__ == "__main__":
    build_cli()
    print(f"StopLight local harness: http://localhost:{PORT}/   (sets in {STORE})")
    ThreadingHTTPServer(("0.0.0.0", PORT), H).serve_forever()
