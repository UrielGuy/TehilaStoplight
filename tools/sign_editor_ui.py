"""Drive the offline editor's page (tools/sign_editor.py) in a headless Edge
and check it: no JS errors, the editor card, the sound of the preview
timeline (play / fade / stop at the frames' starts, as on the sign), the
page's play button, the track names, and Compare -> upload plan.

    python.exe tools/sign_editor_ui.py http://127.0.0.1:8095/ <music folder> <sign target> [screenshot.png]

The workspace should hold a set whose first frame plays track 1 (download a
sign or the harness first). Needs websocket-client (as tools/ui_flow.py).
"""
import json, subprocess, sys, tempfile, time, urllib.request
import websocket

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
URL = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8095/"
MUSIC = sys.argv[2] if len(sys.argv) > 2 else ""
TARGET = sys.argv[3] if len(sys.argv) > 3 else "http://192.168.4.1"
EDGE = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
FAILS = []


def check(name, cond, detail=""):
    print(("  ok    " if cond else "  FAIL  ") + name + (f"  ({str(detail)[:300]})" if not cond else ""))
    if not cond:
        FAILS.append(name)


if MUSIC:
    urllib.request.urlopen(urllib.request.Request(URL.rstrip("/") + "/api/editor/music", data=json.dumps({"dir": MUSIC}).encode(), method="POST",
                                                  headers={"Content-Type": "application/json"})).read()
prof = tempfile.mkdtemp()
p = subprocess.Popen([EDGE, "--headless=new", "--disable-gpu", "--no-first-run", "--remote-debugging-port=9334", "--user-data-dir=" + prof,
                      "--autoplay-policy=no-user-gesture-required", "--window-size=1100,1400", "about:blank"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(3)
tab = [t for t in json.loads(urllib.request.urlopen("http://127.0.0.1:9334/json").read()) if t.get("type") == "page"][0]
ws = websocket.create_connection(tab["webSocketDebuggerUrl"], suppress_origin=True)
mid, errors = 0, []


def send(method, params=None):
    global mid
    mid += 1
    ws.send(json.dumps({"id": mid, "method": method, "params": params or {}}))
    while True:
        m = json.loads(ws.recv())
        if m.get("method") == "Runtime.exceptionThrown":
            errors.append(m["params"]["exceptionDetails"].get("exception", {}).get("description", str(m["params"]["exceptionDetails"])))
        elif m.get("method") == "Runtime.consoleAPICalled" and m["params"]["type"] in ("error", "warning"):
            errors.append("console." + m["params"]["type"] + ": " + " ".join(str(a.get("value", a.get("description"))) for a in m["params"]["args"]))
        if m.get("id") == mid:
            return m.get("result", {})


def js(expr):
    r = send("Runtime.evaluate", {"expression": expr, "awaitPromise": True, "returnByValue": True})
    if "exceptionDetails" in r:
        errors.append("eval: " + r["exceptionDetails"].get("exception", {}).get("description", "?"))
        return None
    return r.get("result", {}).get("value")


try:
    send("Runtime.enable")
    send("Page.enable")
    send("Page.navigate", {"url": URL})
    time.sleep(4)
    check("editor card on top", js("document.querySelector('.wrap').firstElementChild.id") == "edPanel")
    check("workspace path shown", bool(js("document.getElementById('eWs').textContent")))
    check("targets: WiFi + other", (js("[...document.getElementById('eTarget').options].map(o=>o.value).join(' ')") or "").count("http") >= 1)
    check("tracks listed from the music folder", (js("document.getElementById('eTracks').children.length") or 0) >= 1 if MUSIC else True)
    js("localStorage.clear(); 1")
    js("document.querySelector('[data-edit=\"0\"]').click(); 1")
    time.sleep(1.5)
    mp3 = js("ed.frames.map(f=>[f.mp3||0,f.ms,f.fade||0])")
    print("  frames (mp3, ms, fade):", mp3)
    check("set 1 loaded, frame 1 starts track 1", mp3 and mp3[0][0] == 1, mp3)
    labels = js("[...document.getElementById('frameMp3').options].map(o=>o.textContent)")
    check("sound list shows the file names", any("intro" in x for x in labels or []), labels)
    # Play the whole set with sound for one and a half passes.
    js("document.getElementById('live').checked=true; document.getElementById('live').dispatchEvent(new Event('change'));"
       "document.getElementById('playAll').checked=true; document.getElementById('playAll').dispatchEvent(new Event('change')); __audioLog.length=0; 1")
    pl = js("(()=>{const pl=playlist(ed.frames,ed.loops);let t=0;return pl.map(i=>{const r=[t,ed.frames[i].mp3||0,ed.frames[i].fade||3];t+=ed.frames[i].ms;return r}).concat([[t,'end',0]])})()")
    cycle = pl[-1][0]
    time.sleep(cycle / 1000 * 1.5 + 1.5)
    log = js("__audioLog.slice()")
    print("  playlist (start ms, mp3, fade):", pl)
    print("  audio log:", log)
    ev = [e for e in log if e["ev"] in ("play", "fade", "stop", "blocked", "missing")]
    check("no blocked or missing track", not any(e["ev"] in ("blocked", "missing") for e in ev), ev)
    plays = [e for e in ev if e["ev"] == "play"]
    check("track 1 plays at the set's start and again when it starts over", len(plays) >= 2 and all(e["n"] == 1 for e in plays), plays)
    if len(plays) >= 2:
        gap = plays[1]["t"] - plays[0]["t"]
        check(f"restart after one pass ({gap} ms, playlist {cycle} ms)", abs(gap - cycle) < 150, gap)
        want = [(s, m, f) for s, m, f in pl[:-1] if m == -2]
        fades = [e for e in ev if e["ev"] == "fade"]
        if want:
            dt = fades[0]["t"] - plays[0]["t"] if fades else None
            check(f"fade at the first fade-out frame ({dt} ms, frame starts at {want[0][0]} ms)", dt is not None and abs(dt - want[0][0]) < 150, fades)
            stops = [e for e in ev if e["ev"] == "stop" and e["t"] > fades[0]["t"]] if fades else []
            dt2 = stops[0]["t"] - fades[0]["t"] if stops else None
            check(f"fade ends the track after {want[0][2]} s ({dt2} ms)", dt2 is not None and abs(dt2 - want[0][2] * 1000) < 250, stops)
    played = js("new Promise(r=>{const a=new Audio('/music/1');a.volume=0;a.play().then(()=>setTimeout(()=>{r(a.currentTime);a.pause()},700)).catch(e=>r('err '+e))})")
    check("the browser plays the music file", isinstance(played, (int, float)) and played > 0.2, played)
    js("document.getElementById('playAll').checked=false; document.getElementById('playAll').dispatchEvent(new Event('change')); 1")
    time.sleep(0.6)
    check("sound stops when 'play the whole set' is off", js("__audioLog.slice(-1)[0].ev") == "stop", js("__audioLog.slice(-2)"))
    js("__audioLog.length=0; document.getElementById('mp3Test').click(); 1")
    time.sleep(0.5)
    check("the frame's play button plays its track in the browser", (js("__audioLog.map(e=>e.ev+e.n).join()") or "").startswith("play1"), js("__audioLog"))
    js("document.getElementById('mp3Stop').click(); 1")
    # Compare with the sign: the plan appears.
    js(f"document.getElementById('eTarget').value='url'; document.getElementById('eUrl').value={json.dumps(TARGET)}; document.getElementById('eTarget').dispatchEvent(new Event('change')); document.getElementById('ePlan').click(); 1")
    for _ in range(60):
        time.sleep(0.5)
        if js("!document.getElementById('ePlanBox').classList.contains('hide')"):
            break
    check("upload plan shown after reading the sign", js("!document.getElementById('ePlanBox').classList.contains('hide')"), js("document.getElementById('eMsg').textContent"))
    print("  plan:", js("[...document.querySelectorAll('#ePlanRows tr')].map(t=>t.textContent).join(' | ')"), "/", js("document.getElementById('ePlanSpace').textContent"))
    print("  summary:", js("document.getElementById('eSum').textContent"))
    if len(sys.argv) > 4:   # a screenshot of the page, e.g. to look at the card
        import base64
        js("window.scrollTo(0,0); 1")
        shot = send("Page.captureScreenshot", {"captureBeyondViewport": True, "clip": {"x": 0, "y": 0, "width": 1100, "height": 1400, "scale": 1}})
        open(sys.argv[4], "wb").write(base64.b64decode(shot["data"]))
finally:
    time.sleep(0.5)
    check("no JS errors", not errors, errors[:5])
    ws.close()
    p.terminate()
print("FAILED: " + ", ".join(FAILS) if FAILS else "all passed")
sys.exit(1 if FAILS else 0)
