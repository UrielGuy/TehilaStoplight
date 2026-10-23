"""Drive the offline editor's page (tools/sign_editor.py) in a headless Edge
and check it: no JS errors, the editor card, the sound of the preview
timeline (play / fade / stop at the frames' starts, as on the sign), the
page's play button, the track names, and Compare -> upload plan; with
--studio, the New animation card too (open a clip, convert, the reported size
against the .anm file, the space numbers, Add to workspace, replace).

    python.exe tools/sign_editor_ui.py http://127.0.0.1:8095/ <music folder> <sign target> [screenshot.png]
    python.exe tools/sign_editor_ui.py http://127.0.0.1:8099/ --studio clip.gif --studio-only [--studio-shot card.png]

The workspace should hold a set whose first frame plays track 1 (download a
sign or the harness first); --studio-only needs nothing in it. Use a
throwaway workspace: the studio check adds an animation (and removes it again
at the end). Needs websocket-client (as tools/ui_flow.py).
"""
import json, os, re, struct, subprocess, sys, tempfile, time, urllib.request
import websocket

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
ARGS = sys.argv[1:]


def _opt(flag, has_value=True):
    if flag not in ARGS:
        return None
    i = ARGS.index(flag)
    v = ARGS[i + 1] if has_value else True
    del ARGS[i:i + (2 if has_value else 1)]
    return v


STUDIO = _opt("--studio")
STUDIO_SHOT = _opt("--studio-shot")
ONLY = bool(_opt("--studio-only", False))
URL = ARGS[0] if len(ARGS) > 0 else "http://127.0.0.1:8095/"
MUSIC = ARGS[1] if len(ARGS) > 1 else ""
TARGET = ARGS[2] if len(ARGS) > 2 else "http://192.168.4.1"
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


def wait_for(expr, timeout):
    end = time.time() + timeout
    while time.time() < end:
        if js(expr):
            return True
        time.sleep(0.25)
    return False


def kb(b):   # as AnimStudio.KB on the page
    return f"{b / 1048576:.2f} MB" if b >= 1048576 else f"{-(-b // 1024)} KB"


def studio_flow(clip):
    """The New animation card, start to end, against the server's own files."""
    print("--- New animation card")
    base = URL.rstrip("/")
    get = lambda path: json.loads(urllib.request.urlopen(base + path).read())
    cost = lambda n: -(-n // 4096) * 4096
    send("Emulation.setDeviceMetricsOverride", {"width": 1100, "height": 1400, "deviceScaleFactor": 1, "mobile": False})   # the desktop layout
    js("window.confirm=()=>true; 1")
    check("card placed just above 'Your animations'", js("(()=>{const c=document.getElementById('stuCard'),n=c.nextElementSibling;"
                                                          "return c.parentElement.classList.contains('wrap')&&!!n&&n.querySelector('h2').textContent==='Your animations'})()"))
    check("workspace section in the studio's output slot", js("document.getElementById('stuWs').parentElement.id") == "stuOut")
    deps = get("/api/studio/deps")
    check("the missing-package message matches what is installed", js("document.getElementById('stuDeps').hidden") == (deps["ffmpeg"] and deps["ytdlp"]), deps)
    check("the space section is drawn before any clip", "on the sign" in (js("document.getElementById('stuSpMsg').textContent") or ""))
    if not deps["ffmpeg"]:
        check("without ffmpeg the file input is off and the message says how to install it",
              js("document.getElementById('stuFile').disabled") and "pip install" in js("document.getElementById('stuDeps').textContent"))
        return
    name = "stutest"
    while any(a["id"] == name for a in get("/api/state")["anims"]):
        name += "x"
    js("window.__stuProg=[];setInterval(()=>{for(const id of ['stuSrcProg','stuConvProg']){const e=document.getElementById(id);"
       "if(!e.hidden){const t=id+': '+e.querySelector('span').textContent;if(__stuProg[__stuProg.length-1]!==t)__stuProg.push(t)}}},20);1")
    doc = send("DOM.getDocument")
    node = send("DOM.querySelector", {"nodeId": doc["root"]["nodeId"], "selector": "#stuFile"})["nodeId"]
    send("DOM.setFileInputFiles", {"nodeId": node, "files": [os.path.abspath(clip)]})
    ok = wait_for("!document.getElementById('stuEdit').hidden&&!document.getElementById('stuConvert').disabled&&document.getElementById('stuV').readyState>=1&&document.getElementById('stuLen').textContent!==''", 90)
    check("source opened: crop and trim shown", ok, js("document.getElementById('stuSrcMsg').textContent"))
    if not ok:
        return
    stem = os.path.splitext(os.path.basename(clip))[0].lower()
    check("name proposed from the file name", js("document.getElementById('stuName').value") == re.sub(r"[^a-z0-9_-]", "", stem.replace(" ", "-").replace(".", "-"))[:24])
    js("document.getElementById('stuName').value=" + json.dumps(name) + ";document.getElementById('stuConvert').click();1")
    ok = wait_for("(()=>{const m=document.getElementById('stuConvMsg');return m.textContent.startsWith('Converted')||!!m.querySelector('.err')})()", 180)
    msg = js("document.getElementById('stuConvMsg').textContent")
    check("converted", ok and msg.startswith("Converted"), msg)
    if not (ok and msg.startswith("Converted")):
        return
    prog = js("__stuProg") or []
    print("  progress labels:", prog[:3], "...", prog[-2:])
    check("progress bar shown while opening and while converting", any(t.startswith("stuSrcProg") for t in prog) and any(t.startswith("stuConvProg") for t in prog), prog)
    if not any(t.split(": ", 1)[1].startswith(("Preparing the preview", "Reading frames", "Converting colours")) for t in prog):
        print("  (no phase label caught: the requests beat the page's 300 ms poll; a longer clip shows them)")
    S = js("({work:__stu.work,bytes:__stu.bytes,format:__stu.format,frames:__stu.frames})")
    ws = get("/api/editor/status")["workspace"]
    wp = os.path.join(ws, "studio", "work", S["work"] + ".anm")
    data = open(wp, "rb").read()
    print(f"  clip: {S['frames']} frames, {len(data)} bytes, format {data[3]}; message: {msg}")
    check("reported size = the .anm file's size", S["bytes"] == len(data) and kb(len(data)) in msg, (S, len(data), msg))
    check("reported format = the file's", S["format"] == data[3] and data[:3] == b"SAN" and f"format {data[3]}" in msg, S)
    check("the browser preview decoded the clip", js("(()=>{const c=document.getElementById('stuPv');return c.height===128})()"))
    js("document.getElementById('stuName').dispatchEvent(new Event('input'));1")
    time.sleep(1.0)
    sp = js("__stuSpace()")
    C = sp and sp.get("clip")
    check("space report has the clip under its name", C and C["bytes"] == len(data) and C["name"] == name and C["cost"] == cost(len(data)), C)
    if C:
        man = os.path.join(ws, "board.json")
        man = json.load(open(man, encoding="utf-8")) if os.path.exists(man) else {}
        total = man.get("total") or 0x4B0000
        board = man.get("anims") or {}
        overhead = max(0, total - man["free"] - sum(cost(a["bytes"]) for a in board.values())) if man.get("free") is not None else 0
        mine = {a["id"]: a["bytes"] for a in get("/api/state")["anims"]}
        mine[name] = len(data)
        held = sum(cost(b) for b in mine.values()) + sum(cost(a["bytes"]) for k, a in board.items() if k not in mine)
        want = total - overhead - held
        check(f"free after the Upload = total - file system - every animation's blocks ({want})", sp["total"] == total and sp["overhead"] == overhead and C["freeAfter"] == want, sp)
        fit = js("document.getElementById('stuFit').textContent") or ""
        print("  fit line:", fit)
        check("the card says it fits and what is left", C["fits"] and fit.startswith("Fits") and kb(want) in fit, fit)
        check("the bar shows the clip striped", js("!!document.querySelector('#stuBar i.new')"))
    js("document.getElementById('stuAdd').click();1")
    ok = wait_for("document.getElementById('stuAddMsg').textContent.startsWith('Added')", 15)
    check("Add to workspace", ok, js("document.getElementById('stuAddMsg').textContent"))
    st = get("/api/state")
    got = next((a for a in st["anims"] if a["id"] == name), None)
    check("the workspace has it, same bytes", got and got["bytes"] == len(data) and open(os.path.join(ws, "anims", name + ".anm"), "rb").read() == data, got)
    time.sleep(1.5)
    check("listed in 'Your animations'", js(f"[...document.querySelectorAll('#animList li')].some(li=>li.textContent.includes({json.dumps(name)}))"))
    check("in the lights' graphic lists", js(f"[...document.querySelectorAll('#lights option')].some(o=>o.value==='u:'+{json.dumps(name)})"))
    n = struct.unpack_from("<H", data, 8)[0]
    check("the page's preview decodes it from the workspace", js(f"animData({json.dumps(name)}).then(a=>a&&a.frames.length)") == n)
    check("after adding: the space shows it as in the workspace", js("__stuSpace().clip===null") and js(f"__stuSpace().holdings.some(h=>h.id==={json.dumps(name)}&&h.kind==='workspace')"))
    # Same name again: asked, then replaced (the old copy goes to the trash); refused when the answer is no.
    js("window.confirm=()=>false;document.getElementById('stuConvert').click();1")
    wait_for("document.getElementById('stuAdd').disabled===false&&document.getElementById('stuAddMsg').textContent===''", 120)
    js("document.getElementById('stuAdd').click();1")
    check("same name, answer no: not added", wait_for("document.getElementById('stuAddMsg').textContent.startsWith('Not added')", 10), js("document.getElementById('stuAddMsg').textContent"))
    js("window.confirm=()=>true;document.getElementById('stuAdd').click();1")
    check("same name, answer yes: replaced", wait_for("document.getElementById('stuAddMsg').textContent.startsWith('Added')", 10))
    trash = os.path.join(ws, "trash")
    check("the replaced copy is in the trash", os.path.isdir(trash) and any(os.path.exists(os.path.join(trash, d, name + ".anm")) for d in os.listdir(trash)))
    if STUDIO_SHOT:
        import base64
        r = js("(()=>{const b=document.getElementById('stuCard').getBoundingClientRect();return [b.left+scrollX,b.top+scrollY,b.width,b.height]})()")
        shot = send("Page.captureScreenshot", {"captureBeyondViewport": True, "clip": {"x": r[0], "y": r[1], "width": r[2], "height": r[3], "scale": 1}})
        open(STUDIO_SHOT, "wb").write(base64.b64decode(shot["data"]))
    urllib.request.urlopen(urllib.request.Request(base + "/api/anims/" + name, method="DELETE")).read()   # leave the workspace as it was


try:
    send("Runtime.enable")
    send("Page.enable")
    send("Page.navigate", {"url": URL})
    time.sleep(4)
    if ONLY:
        studio_flow(STUDIO)
        raise SystemExit
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
    if len(ARGS) > 3:   # a screenshot of the page, e.g. to look at the card
        import base64
        js("window.scrollTo(0,0); 1")
        shot = send("Page.captureScreenshot", {"captureBeyondViewport": True, "clip": {"x": 0, "y": 0, "width": 1100, "height": 1400, "scale": 1}})
        open(ARGS[3], "wb").write(base64.b64decode(shot["data"]))
    if STUDIO:
        studio_flow(STUDIO)
except SystemExit:
    pass
finally:
    time.sleep(0.5)
    check("no JS errors", not errors, errors[:5])
    ws.close()
    p.terminate()
print("FAILED: " + ", ".join(FAILS) if FAILS else "all passed")
sys.exit(1 if FAILS else 0)
