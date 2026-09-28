#!/usr/bin/env python3
"""Imitate a person typing on the web page against a device or the harness.

    python3 tools/type_test.py http://192.168.4.1 "Please wait for the light" [--live] [--delay 0.15]

Sends the text one character at a time: after each character it does what the
page does, a /api/check for the field, a /api/preview.rle fetch, and (with
--live) a POST /api/live with the draft. Prints timing per step and stops on
the first HTTP failure so a crash/reboot shows up as a timeout.
"""
import json, sys, time, urllib.request, urllib.parse

base = sys.argv[1].rstrip("/")
text = sys.argv[2] if len(sys.argv) > 2 else "Please wait for the light"
live = "--live" in sys.argv
delay = float(sys.argv[sys.argv.index("--delay") + 1]) if "--delay" in sys.argv else 0.15

def get(path, timeout=4):
    with urllib.request.urlopen(base + path, timeout=timeout) as r:
        return r.read()

def post(path, obj, timeout=4):
    req = urllib.request.Request(base + path, data=json.dumps(obj).encode(), headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()

print("state:", get("/api/state")[:80])
draft = ""
for ch in text:
    draft += ch
    t0 = time.time()
    try:
        chk = json.loads(get("/api/check?text=" + urllib.parse.quote(draft) + "&light=0&size=0"))
        t1 = time.time()
        rle = get("/api/preview.rle?t0=" + urllib.parse.quote(draft) + "&s0=0&c0=48&f0=0&m=7&k=0")
        t2 = time.time()
        if live:
            post("/api/live", {"t": [draft, "", ""], "s": [0, 0, 0], "c": [48, 52, 12], "f": [0, 0, 0], "g": ["", "", ""], "a": "all", "p": 1000})
        t3 = time.time()
        print(f"{draft!r:34} check {1000*(t1-t0):4.0f} ms  rle {1000*(t2-t1):4.0f} ms ({len(rle)} B)  live {1000*(t3-t2):4.0f} ms  fits={chk.get('ok')} size={chk.get('size')}")
    except Exception as e:
        print(f"{draft!r}: FAILED after {1000*(time.time()-t0):.0f} ms: {e}")
        sys.exit(1)
    time.sleep(delay)
if live:
    post("/api/live", {}, timeout=4) if False else None
    req = urllib.request.Request(base + "/api/live", method="DELETE")
    urllib.request.urlopen(req, timeout=4).read()
print("done, device still answering:", get("/api/state")[:40])
