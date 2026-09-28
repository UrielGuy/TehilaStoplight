"""Drive the real web page in a headless Edge through the DevTools protocol and
report JavaScript errors. Runs on Windows Python (python.exe) against the local
harness (python3 tools/local_server.py) or the device:

    python.exe tools/ui_flow.py [http://localhost:8080/]

Needs `python.exe -m pip install --user websocket-client`. Each step evaluates
JS in the page (clicks and input events go through the page's own handlers).
Edit the `step(...)` list to script a different flow.
"""
import json, subprocess, time, urllib.request, websocket, sys, os, tempfile
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
EDGE = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
prof = tempfile.mkdtemp()
p = subprocess.Popen([EDGE, "--headless=new", "--disable-gpu", "--no-first-run", "--remote-debugging-port=9333", "--user-data-dir=" + prof, "--window-size=420,900", "about:blank"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(3)
tabs = json.loads(urllib.request.urlopen("http://127.0.0.1:9333/json").read())
tab = [t for t in tabs if t.get("type") == "page"][0]
ws = websocket.create_connection(tab["webSocketDebuggerUrl"], suppress_origin=True)
mid = 0; errors = []
def send(method, params=None):
    global mid; mid += 1
    ws.send(json.dumps({"id": mid, "method": method, "params": params or {}}))
    while True:
        m = json.loads(ws.recv())
        if m.get("method") == "Runtime.exceptionThrown":
            errors.append(m["params"]["exceptionDetails"].get("exception", {}).get("description", str(m["params"]["exceptionDetails"])))
        elif m.get("method") == "Runtime.consoleAPICalled" and m["params"]["type"] in ("error", "warning"):
            errors.append("console." + m["params"]["type"] + ": " + " ".join(str(a.get("value", a.get("description"))) for a in m["params"]["args"]))
        if m.get("id") == mid: return m.get("result", {})
def js(expr):
    r = send("Runtime.evaluate", {"expression": expr, "awaitPromise": True, "returnByValue": True})
    if "exceptionDetails" in r: errors.append("eval: " + r["exceptionDetails"].get("exception", {}).get("description", "?")); return None
    return r.get("result", {}).get("value")
send("Runtime.enable"); send("Page.enable")
time.sleep(2.5)
step = lambda name, expr, wait=0.8: (print(f"{name:34}", js(expr)), time.sleep(wait))
step("sets listed", "document.querySelectorAll('[data-edit]').length")
step("click Edit on set 1", "document.querySelector('[data-edit=\"0\"]').click(); 'ok'", 2.0)
step("frames in editor", "ed.frames.length + ' frames, cur ' + ed.cur + ', loops ' + JSON.stringify(ed.loops) + ', editing ' + ed.editing")
step("select frame 12", "[...document.querySelectorAll('#frames button')].find(b=>b.textContent==='12').click(); 'cur=' + ed.cur", 1.5)
step("click Duplicate", "document.getElementById('dupFrame').click(); ed.frames.length + ' frames, cur ' + ed.cur + ', loops ' + JSON.stringify(ed.loops)", 1.5)
step("change the new frame's text", "const t=document.getElementById('t0'); t.value='שלום!'; t.dispatchEvent(new Event('input')); 'typed'", 1.5)
step("loop from=11", "const a=document.querySelectorAll('#loops .loop input')[0]; a.value=11; a.dispatchEvent(new Event('change')); JSON.stringify(ed.loops)", 1.0)
step("loop to=14", "const b=document.querySelectorAll('#loops .loop input')[1]; b.value=14; b.dispatchEvent(new Event('change')); JSON.stringify(ed.loops) + ' errors ' + JSON.stringify(loopErrors())", 1.0)
step("name", "const n=document.getElementById('name'); n.value='Repro'; n.dispatchEvent(new Event('input')); ed.name", 0.5)
step("Save enabled?", "!document.getElementById('add').disabled + ' label=' + document.getElementById('add').textContent")
step("click Save", "document.getElementById('add').click(); 'clicked'", 3.0)
step("message / editor after save", "document.getElementById('msg').textContent + ' | editing=' + ed.editing + ' frames=' + ed.frames.length")
step("list after save", "[...document.querySelectorAll('#list li .nm')].map(e=>e.textContent).join(',') + ' | ' + [...document.querySelectorAll('#list li em')].map(e=>e.textContent).join(' ; ')")
time.sleep(1.5)
print("JS errors:", len(errors))
for e in errors[:10]: print("  ", e[:300])
ws.close(); p.terminate()
