"""Check tools/anim_v3.js in a real browser: decode every .pio/v3/<name>.v3
in headless Edge and compare every pixel with <name>.raw (written by
`python3 tools/anim_v3.py --prepare .pio/samples .pio/v3`).  Windows Python:

    python.exe tools/anim_v3_jstest.py [dir=.pio/v3]

Serves the repo root on http://127.0.0.1:8095 with http.server, starts Edge
with --remote-debugging-port=9334 (needs `python.exe -m pip install --user
websocket-client`), loads the script with fetch + eval, decodes each clip
in the page and prints the decode time per clip.  Exit code 1 on mismatch.
"""
import json, os, subprocess, sys, tempfile, time, urllib.request
import websocket

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
EDGE = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
HTTP_PORT, CDP_PORT = 8095, 9334
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sub = sys.argv[1] if len(sys.argv) > 1 else os.path.join(".pio", "v3")
names = sorted(f[:-3] for f in os.listdir(os.path.join(ROOT, sub)) if f.endswith(".v3"))
if not names:
    sys.exit(f"no .v3 files in {sub}")
url_dir = "/" + sub.replace("\\", "/").strip("/") + "/"

server = subprocess.Popen([sys.executable, "-m", "http.server", str(HTTP_PORT), "--bind", "127.0.0.1", "--directory", ROOT],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
prof = tempfile.mkdtemp()
edge = subprocess.Popen([EDGE, "--headless=new", "--disable-gpu", "--no-first-run", f"--remote-debugging-port={CDP_PORT}",
                         "--user-data-dir=" + prof, f"http://127.0.0.1:{HTTP_PORT}/tools/"],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
try:
    for _ in range(30):
        try:
            tabs = json.loads(urllib.request.urlopen(f"http://127.0.0.1:{CDP_PORT}/json").read())
            tab = [t for t in tabs if t.get("type") == "page"][0]
            break
        except Exception:
            time.sleep(0.5)
    else:
        sys.exit("Edge did not start")
    ws = websocket.create_connection(tab["webSocketDebuggerUrl"], suppress_origin=True)
    ws.settimeout(120)
    mid = 0

    def send(method, params=None):
        global mid
        mid += 1
        ws.send(json.dumps({"id": mid, "method": method, "params": params or {}}))
        while True:
            m = json.loads(ws.recv())
            if m.get("id") == mid:
                return m.get("result", {})

    def js(expr):
        r = send("Runtime.evaluate", {"expression": expr, "awaitPromise": True, "returnByValue": True})
        if "exceptionDetails" in r:
            sys.exit("JS error: " + r["exceptionDetails"].get("exception", {}).get("description", str(r["exceptionDetails"])))
        return r.get("result", {}).get("value")

    send("Runtime.enable")
    time.sleep(1.5)
    js("(async () => { const s = await (await fetch('/tools/anim_v3.js')).text(); (0, eval)(s); return typeof decodeV3; })()")
    print(f"{'clip':24s} {'frames':>6s} {'bytes':>8s} {'decode ms':>9s} {'ms/frame':>8s}  result")
    failed = 0
    for name in names:
        res = js(f"""(async () => {{
          const v3 = await (await fetch('{url_dir}{name}.v3')).arrayBuffer();
          const raw = new Uint8Array(await (await fetch('{url_dir}{name}.raw')).arrayBuffer());
          const t0 = performance.now(); const r = decodeV3(v3); const ms = performance.now() - t0;
          const w = raw[0] | raw[1] << 8, h = raw[2] | raw[3] << 8, n = raw[4] | raw[5] << 8;
          let bad = '';
          if (r.w !== w || r.h !== h || r.frames.length !== n) bad = 'header ' + r.w + 'x' + r.h + ' n=' + r.frames.length;
          for (let i = 0; i < n && !bad; i++) {{
            const ref = raw.subarray(6 + i * w * h, 6 + (i + 1) * w * h), fr = r.frames[i];
            for (let k = 0; k < w * h; k++) if (fr[k] !== ref[k]) {{ bad = 'frame ' + i + ' pixel ' + k; break; }}
          }}
          if (!bad && r.delays.some(x => x < 20)) bad = 'delay < 20';
          if (!bad && r.total !== r.delays.reduce((a, b) => a + b, 0)) bad = 'total';
          return {{ n: n, bytes: v3.byteLength, ms: ms, bad: bad, total: r.total }};
        }})()""")
        ok = not res["bad"]
        failed += not ok
        print(f"{name:24s} {res['n']:6d} {res['bytes']:8d} {res['ms']:9.0f} {res['ms'] / res['n']:8.2f}  {'OK' if ok else 'MISMATCH ' + res['bad']}")
    print("ALL OK" if not failed else f"FAILED ({failed})")
    ws.close()
finally:
    edge.terminate()
    server.terminate()
sys.exit(1 if failed else 0)
