#!/usr/bin/env python3
"""End-to-end test of tools/sign_editor.py over WiFi, with the local harness
(tools/local_server.py) as the sign: download, edit, compare, plan, upload,
resume, music folder, preview, the New animation card's endpoints. Plus unit
tests of the upload plan (space, order) and of the space a new clip takes.
Everything runs in temporary folders on free ports; the real
tools/local_sets.json and tools/gfx are not touched.

    python3 tools/sign_editor_test.py        # WSL (or any Linux with g++)
    python.exe tools/sign_editor_test.py --studio   # only the card's conversion (needs imageio-ffmpeg)

The conversion part runs whenever this Python has ffmpeg (imageio-ffmpeg);
under WSL it usually has not, and says so.
"""
import json, os, shutil, socket, subprocess, sys, tempfile, time, urllib.request, uuid

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sign_editor as se   # noqa: E402
import sign_link as sl     # noqa: E402

FAILS = []


def check(name, cond, detail=""):
    print(("  ok    " if cond else "  FAIL  ") + name + (f"  ({str(detail)[:400]})" if detail and not cond else ""))
    if not cond:
        FAILS.append(name)


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def anm(frames=2, extra=0):
    """A small valid version 1 file (128x128, RLE frames of one colour)."""
    runs = bytes([255, 12]) * 64 + bytes([64, 12])
    table = b"".join((100).to_bytes(2, "little") + len(runs).to_bytes(4, "little") for _ in range(frames))
    return b"SAN\x01" + (128).to_bytes(2, "little") + (128).to_bytes(2, "little") + frames.to_bytes(2, "little") + b"\0\0" + table + runs * frames + b"\0" * extra


def fr(t=("", "", ""), g=("", "", ""), k=("short", "short", "short"), **kw):
    f = {"t": list(t), "k": list(k), "g": list(g), "s": [0, 0, 0], "c": [48, 52, 12], "f": [0, 0, 0], "ms": 1000}
    f.update(kw)
    return f


def req(base, method, path, body=None, raw=None, headers=None):
    data = raw if raw is not None else (json.dumps(body).encode() if body is not None else None)
    r = urllib.request.Request(base + path, data=data, method=method, headers=headers or ({"Content-Type": "application/json"} if data else {}))
    try:
        with urllib.request.urlopen(r, timeout=60) as resp:
            b, code = resp.read(), resp.status
    except urllib.error.HTTPError as e:
        b, code = e.read(), e.code
    try:
        return code, json.loads(b)
    except ValueError:
        return code, b


def wait_job(base, timeout=120):
    end = time.time() + timeout
    while time.time() < end:
        j = req(base, "GET", "/api/editor/status")[1]["job"]
        if j["state"] != "running":
            return j
        time.sleep(0.2)
    return {"state": "timeout"}


def multipart(name, data):
    b = uuid.uuid4().hex
    body = (f"--{b}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"{name}.anm\"\r\nContent-Type: application/octet-stream\r\n\r\n").encode() + data + f"\r\n--{b}--\r\n".encode()
    return body, {"Content-Type": f"multipart/form-data; boundary={b}"}


def plan_units():
    print("--- upload plan")
    tmp = tempfile.mkdtemp(prefix="edplan-")
    ws = se.Workspace(tmp)
    big, small = anm(2, 300000), anm(2, 1000)
    open(ws.anim_path("big"), "wb").write(big)
    open(ws.anim_path("keep"), "wb").write(small)
    ws.data["sets"] = [{"name": "a", "frames": [fr(g=("u:big", "", ""))]}]
    ws.save_manifest({"anims": {"keep": {"bytes": len(small), "crc": se.crc32(small)}, "old": {"bytes": 200000, "crc": 1}}})
    look = {"anims": [{"id": "keep", "bytes": len(small)}, {"id": "old", "bytes": 200000}], "free": 150000, "total": se.ANIM_SPACE,
            "sets": [{"name": "s", "frames": [fr(g=("u:old", "", ""))]}], "settings": {"offDark": False, "rotate180": True, "colourRotated": True, "volume": 20},
            "sums": {}, "via": "test", "kind": "wifi"}
    p = se.make_plan(ws, look, False)
    check("plan: big does not fit while 'old' stays", not p["ok"] and any("big" in e for e in p["errors"]), p["errors"])
    p = se.make_plan(ws, look, True)
    ops = [(s["op"], s.get("id")) for s in p["steps"]]
    check("plan: deleting 'old' (used by the sign's sets) is pulled before 'big' to make room", p["ok"] and ops[0] == ("delete", "old") and ops[1] == ("add", "big") and ops[2][0] == "sets", ops)
    look["free"] = 2000000
    p = se.make_plan(ws, look, True)
    ops = [(s["op"], s.get("id")) for s in p["steps"]]
    check("plan: with room, 'old' goes after the new sets", ops == [("add", "big"), ("sets", None), ("delete", "old")], ops)
    check("plan: 'keep' (same CRC as downloaded) is not sent", all(s.get("id") != "keep" for s in p["steps"]))
    look["anims"][0]["bytes"] += 1   # changed on the sign since the download
    p = se.make_plan(ws, look, True)
    check("plan: a copy that changed on the sign is replaced", any(s.get("id") == "keep" and s["op"] == "replace" for s in p["steps"]) and p["warnings"], p)
    look["anims"][0]["bytes"] -= 1
    look["sums"] = {"keep": 123}
    p = se.make_plan(ws, look, True)
    check("plan: a cable checksum that differs means replace", any(s.get("id") == "keep" for s in p["steps"]))
    ws.data["sets"] = [{"name": "a", "frames": [fr(g=("u:gone", "", ""))]}]
    p = se.make_plan(ws, look, True)
    check("plan: a set using a missing animation is refused", not p["ok"] and any("gone" in e for e in p["errors"]))
    shutil.rmtree(tmp, ignore_errors=True)


def space_units():
    print("--- space a new clip takes (New animation card)")
    tmp = tempfile.mkdtemp(prefix="edspace-")
    ws = se.Workspace(tmp)
    a, b = anm(2, 100000), anm(2, 50000)
    open(ws.anim_path("a"), "wb").write(a)
    open(ws.anim_path("b"), "wb").write(b)
    T, O, cost = se.ANIM_SPACE, 40960, se.cost
    F = T - O - cost(len(a)) - cost(30000)   # the sign has a (as here) and c, plus O of file system
    ws.save_manifest({"anims": {"a": {"bytes": len(a), "crc": se.crc32(a)}, "c": {"bytes": 30000, "crc": 1}}, "free": F, "total": T,
                      "sets": [], "settings": {}, "download": {"at": 1, "via": "test"}})
    r = se.space_report(ws)
    check("space: overhead from board.json", r["overhead"] == O and r["total"] == T and r["source"] == "board.json", r)
    check("space: the workspace alone (b goes up, c stays on the sign)", r["base"]["fits"] and r["base"]["freeAfter"] == F - cost(len(b))
          and {(h["id"], h["kind"]) for h in r["holdings"]} == {("a", "workspace"), ("b", "workspace"), ("c", "sign")} and r["clip"] is None, r)
    r = se.space_report(ws, 200000, "New Clip")
    C = r["clip"]
    check("space: a new clip", C["name"] == "new-clip" and C["fits"] and C["missing"] == 0 and C["cost"] == cost(200000) and C["replaces"] is None
          and C["freeAfter"] == F - cost(len(b)) - cost(200000) and any(h["id"] == "new-clip" and h["kind"] == "new" for h in r["holdings"]), r)
    r = se.space_report(ws, 200000, "new-clip", delete_removed=True)
    check("space: with 'delete the animations not in the workspace' c's room counts", r["clip"]["freeAfter"] == F - cost(len(b)) - cost(200000) + cost(30000)
          and not any(h["id"] == "c" for h in r["holdings"]), r)
    r = se.space_report(ws, 200000, "a")
    check("space: replacing a workspace animation frees its old blocks", r["clip"]["replaces"] == len(a) and r["clip"]["freeAfter"] == F - cost(len(b)) + cost(len(a)) - cost(200000)
          and [h["kind"] for h in r["holdings"] if h["id"] == "a"] == ["new"], r)
    room = F - cost(len(b))   # b (smaller) goes up first, the clip last
    r = se.space_report(ws, room - se.MARGIN, "big")
    check("space: exactly fits with the upload margin", r["clip"]["fits"] and r["clip"]["missing"] == 0, r["clip"])
    r = se.space_report(ws, room - se.MARGIN + 1000, "big")
    check("space: 1000 bytes too big -> 1000 missing", not r["clip"]["fits"] and r["clip"]["missing"] == 1000 and r["base"]["fits"], r["clip"])
    r = se.space_report(ws, 120000, "")
    check("space: a clip without a name yet", r["clip"]["name"] == "" and r["clip"]["fits"] and any(h["kind"] == "new" for h in r["holdings"]), r["clip"])
    se.LOOK.update({"anims": [{"id": "a", "bytes": len(a)}], "free": F + 12345, "total": T, "sets": [], "settings": {}, "sums": {}, "via": "cmp", "kind": "wifi", "at": 2})
    r = se.space_report(ws)
    check("space: a Compare newer than board.json wins", r["source"] == "compare" and r["base"]["freeAfter"] == F + 12345 - cost(len(b)), r)
    se.LOOK.clear()
    shutil.rmtree(tmp, ignore_errors=True)


def studio_endpoints(ed, wsd):
    """The New animation card's server side, without converting (a clip is put
    where a conversion leaves it)."""
    print("--- New animation card: endpoints")
    page = req(ed, "GET", "/")[1]
    check("page has the New animation card and the shared studio script", b'id="stuCard"' in page and b"window.AnimStudio=function" in page and b"<!--STUDIO-->" not in page)
    c, d = req(ed, "GET", "/api/studio/deps")
    check("deps reported", c == 200 and isinstance(d.get("ffmpeg"), bool) and isinstance(d.get("ytdlp"), bool), d)
    if not d["ffmpeg"]:
        c, r = req(ed, "POST", "/api/studio/open-file", raw=b"GIF89a", headers={"X-Filename": "x.gif"})
        check("without ffmpeg: a clear error with the pip command", c == 400 and "pip install" in r.get("error", "") and "imageio-ffmpeg" in r["error"], r)
    if not d["ytdlp"]:
        c, r = req(ed, "POST", "/api/studio/open-url", {"url": "https://example.com/v"})
        check("without yt-dlp: links say what to install", c == 400 and "pip install" in r.get("error", ""), r)
    check("progress of an unknown request is empty", req(ed, "GET", "/api/studio/progress?t=nope")[1] == {})
    check("no preview video for an unknown source", req(ed, "GET", "/api/studio/media/0123456789.mp4")[0] == 404)
    c, r = req(ed, "POST", "/api/studio/convert", {"id": "0123456789"})
    check("convert of an unknown source", c == 400 and "open it again" in r.get("error", ""), r)
    work = os.path.join(wsd, "studio", "work")
    os.makedirs(work, exist_ok=True)
    data = anm(3, 77777)
    open(os.path.join(work, "abc123def0.anm"), "wb").write(data)
    base = req(ed, "GET", "/api/editor/space")[1]
    sp = req(ed, "GET", "/api/editor/space?work=abc123def0&name=Studio%20Clip")[1]
    C = sp["clip"]
    check("space with the converted clip: its size and blocks", C and C["name"] == "studio-clip" and C["bytes"] == len(data) and C["cost"] == se.cost(len(data)), sp)
    check("space: free after = without it - its blocks", C and C["freeAfter"] == base["base"]["freeAfter"] - se.cost(len(data)) and sp["total"] == base["total"], (base, sp))
    c, r = req(ed, "POST", "/api/editor/addanim", {"work": "abc123def0", "name": ""})
    check("add without a name is refused", c == 400 and "name" in r.get("error", ""), r)
    c, r = req(ed, "POST", "/api/editor/addanim", {"work": "fff", "name": "x"})
    check("add without a conversion is refused", c == 400, r)
    c, r = req(ed, "POST", "/api/editor/addanim", {"work": "abc123def0", "name": "Studio Clip"})
    check("add to the workspace", c == 200 and r["id"] == "studio-clip" and r["bytes"] == len(data) and any(a["id"] == "studio-clip" for a in r["anims"]), r)
    check("the workspace file is the converted clip", open(os.path.join(wsd, "anims", "studio-clip.anm"), "rb").read() == data)
    S = req(ed, "GET", "/api/editor/status")[1]["summary"]
    check("the summary lists it as new (it goes up with the next Upload)", "studio-clip" in S.get("animsAdded", []), S)
    data2 = anm(4, 1234)
    open(os.path.join(work, "abc123def0.anm"), "wb").write(data2)
    c, r = req(ed, "POST", "/api/editor/addanim", {"work": "abc123def0", "name": "studio-clip"})
    check("same name again: asks first (409)", c == 409 and r.get("exists") and r.get("bytes") == len(data), r)
    sp = req(ed, "GET", "/api/editor/space?work=abc123def0&name=studio-clip")[1]
    check("space: the clip would replace the workspace's copy", sp["clip"]["replaces"] == len(data), sp["clip"])
    c, r = req(ed, "POST", "/api/editor/addanim", {"work": "abc123def0", "name": "studio-clip", "replace": True})
    check("same name, replace: new content, old copy in the trash", c == 200 and open(os.path.join(wsd, "anims", "studio-clip.anm"), "rb").read() == data2
          and any(os.path.exists(os.path.join(wsd, "trash", t, "studio-clip.anm")) for t in os.listdir(os.path.join(wsd, "trash"))), r)


def studio_convert():
    """A real conversion through the card's endpoints (needs ffmpeg in this Python)."""
    import anim_studio
    if not anim_studio.deps()["ffmpeg"]:
        print("--- New animation card: conversion skipped (no imageio-ffmpeg in this Python; run python.exe tools/sign_editor_test.py --studio)")
        return
    print("--- New animation card: conversion")
    import threading
    import anim_convert as ac
    tmp = tempfile.mkdtemp(prefix="edstudio-")
    wsd, clip = os.path.join(tmp, "ws"), os.path.join(tmp, "test clip.mp4")
    subprocess.run([ac.ffmpeg_exe(), "-v", "error", "-y", "-f", "lavfi", "-i", "testsrc2=duration=20:size=960x540:rate=25", "-pix_fmt", "yuv420p", clip], check=True)
    ep = free_port()
    ed = f"http://127.0.0.1:{ep}"
    proc = subprocess.Popen([sys.executable, os.path.join(HERE, "sign_editor.py"), "--port", str(ep), "--workspace", wsd, "--no-browser"],
                            stdout=subprocess.DEVNULL, stderr=open(os.path.join(tmp, "editor.log"), "w"))
    try:
        for _ in range(100):
            try:
                urllib.request.urlopen(ed + "/api/state", timeout=2).read()
                break
            except Exception:
                time.sleep(0.2)
        phases, stop = set(), []

        def poll(tok):
            while not stop:
                p = req(ed, "GET", "/api/studio/progress?t=" + tok)[1]
                if p:
                    phases.add(p["phase"])
                time.sleep(0.03)
        threading.Thread(target=poll, args=("t1",), daemon=True).start()
        c, o = req(ed, "POST", "/api/studio/open-file", raw=open(clip, "rb").read(), headers={"X-Filename": "test%20clip.mp4", "X-Token": "t1"})
        check("open a video file", c == 200 and o.get("name") == "test clip" and len(o.get("id", "")) == 10, o)
        threading.Thread(target=poll, args=("t2",), daemon=True).start()
        for shape, crop in (("circle", [0.2, 0, 0.5, 1]), ("full", [0.4, 0, 0.2, 1])):
            c, r = req(ed, "POST", "/api/studio/convert", {"id": o["id"], "shape": shape, "fps": 12, "start": 1, "duration": 3, "crop": crop}, headers={"X-Token": "t2", "Content-Type": "application/json"})
            path = os.path.join(wsd, "studio", "work", o["id"] + ".anm")
            data = open(path, "rb").read() if os.path.exists(path) else b""
            check(f"convert ({shape}): reported size = the .anm file", c == 200 and r["bytes"] == len(data) > 0 and r["format"] == data[3] and 35 <= r["frames"] <= 37, {k: v for k, v in r.items() if k != "anm"})
            h = se.anim_header(data)
            check(f"convert ({shape}): the file's picture size", h and (h["w"], h["h"]) == ((128, 384) if shape == "full" else (128, 128)), h)
            sp = req(ed, "GET", f"/api/editor/space?work={o['id']}&name=clip-{shape}")[1]
            check(f"convert ({shape}): fit numbers", sp["clip"]["bytes"] == len(data) and sp["clip"]["fits"] and sp["clip"]["freeAfter"] == se.ANIM_SPACE - se.cost(len(data)) - sum(se.cost(a["bytes"]) for a in req(ed, "GET", "/api/state")[1]["anims"]), sp["clip"])
            c, a = req(ed, "POST", "/api/editor/addanim", {"work": o["id"], "name": f"clip-{shape}"})
            check(f"convert ({shape}): added to the workspace", c == 200 and a["bytes"] == len(data), a)
        # A still, flat clip: format 3's 7.4 KB model costs more than it saves, so the converter keeps format 2.
        solid = os.path.join(tmp, "solid.gif")
        subprocess.run([ac.ffmpeg_exe(), "-v", "error", "-y", "-f", "lavfi", "-i", "color=c=red:size=320x240:duration=1:rate=10", solid], check=True)
        c, s2 = req(ed, "POST", "/api/studio/open-file", raw=open(solid, "rb").read(), headers={"X-Filename": "solid.gif"})
        c, r = req(ed, "POST", "/api/studio/convert", {"id": s2.get("id"), "shape": "circle", "fps": 12}, headers={"Content-Type": "application/json"})
        path = os.path.join(wsd, "studio", "work", str(s2.get("id")) + ".anm")
        data = open(path, "rb").read() if os.path.exists(path) else b""
        check("a tiny clip stays format 2 (smaller), size = the file", c == 200 and r["format"] == 2 == data[3] and r["bytes"] == len(data), {k: v for k, v in r.items() if k != "anm"})
        stop.append(1)
        check("progress reported (preview, frames, colours or compressing)", {"prepare", "decode"} <= phases and bool({"colours", "encode"} & phases), phases)
        st = req(ed, "GET", "/api/state")[1]
        check("both clips in the workspace's list", {a["id"]: a["full"] for a in st["anims"]} == {"clip-circle": False, "clip-full": True}, st["anims"])
    finally:
        proc.terminate()
        proc.wait(5)
        if FAILS:
            print("  editor log:", open(os.path.join(tmp, "editor.log")).read()[-1500:])
        shutil.rmtree(tmp, ignore_errors=True)


def e2e():
    print("--- WiFi round trip with the harness as the sign")
    tmp = tempfile.mkdtemp(prefix="edtest-")
    fb, wsd, music = os.path.join(tmp, "sign"), os.path.join(tmp, "ws"), os.path.join(tmp, "music")
    os.makedirs(os.path.join(fb, "gfx"))
    os.makedirs(music)
    files = {"froggy": anm(3, 5000), "car-race": anm(2, 2000), "skyroads": anm(4, 100)}
    for k, v in files.items():
        open(os.path.join(fb, "gfx", k + ".anm"), "wb").write(v)
    sets = [{"name": "Morning", "bpm": 120, "frames": [fr(("STOP", "המתן", "GO"), mp3=1), fr(g=("", "u:froggy", "")), fr(("A", "B", "C"), mp3=-2, fade=1)], "loops": [{"from": 2, "to": 3, "count": 2}]},
            {"name": "Race", "bpm": 100, "frames": [fr(g=("u:car-race", "", "fx:pulse"), ms=2000)], "loops": []}]
    json.dump({"sets": sets, "current": 0, "volume": 18, "offDark": False, "rotate180": True, "colourRotated": True}, open(os.path.join(fb, "sets.json"), "w"), ensure_ascii=False)
    for n in ("b.mp3", "A.mp3", "c.wav", "notes.txt"):
        open(os.path.join(music, n), "wb").write(b"ID3" + n.encode() * 100)
    sp, ep = free_port(), free_port()
    sign, ed = f"http://127.0.0.1:{sp}", f"http://127.0.0.1:{ep}"
    env = dict(os.environ, STOPLIGHT_SETS=os.path.join(fb, "sets.json"), STOPLIGHT_GFX=os.path.join(fb, "gfx"))
    procs = [subprocess.Popen([sys.executable, os.path.join(HERE, "local_server.py"), str(sp)], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL),
             subprocess.Popen([sys.executable, os.path.join(HERE, "sign_editor.py"), "--port", str(ep), "--workspace", wsd, "--no-browser"], stdout=subprocess.DEVNULL, stderr=open(os.path.join(tmp, "editor.log"), "w"))]
    try:
        for base in (sign, ed):
            for _ in range(100):
                try:
                    urllib.request.urlopen(base + "/api/state", timeout=2).read()
                    break
                except Exception:
                    time.sleep(0.2)
        st = req(ed, "GET", "/api/editor/status")[1]
        check("fresh workspace: not downloaded yet", st["summary"] == {"known": False}, st["summary"])
        page = req(ed, "GET", "/")[1]
        check("page = the sign's page + the editor card", b"id=\"edPanel\"" in page and b"function timeline()" in page and page.rstrip().endswith(b"</html>"))

        req(ed, "POST", "/api/editor/download", {"target": sign})
        j = wait_job(ed)
        check("download finished", j["state"] == "done", j)
        s = req(ed, "GET", "/api/state")[1]
        check("workspace sets = the sign's", sl.canon_sets(s["sets"]) == sl.canon_sets(sets))
        check("workspace animations = the sign's", {a["id"]: a["bytes"] for a in s["anims"]} == {k: len(v) for k, v in files.items()})
        check("workspace volume / settings = the sign's", s["mp3"]["volume"] == 18 and s["settings"]["rotate180"] is True)
        check("files byte for byte", all(open(os.path.join(wsd, "anims", k + ".anm"), "rb").read() == v for k, v in files.items()))
        st = req(ed, "GET", "/api/editor/status")[1]
        check("after download: same as the sign", st["summary"]["known"] and not st["summary"]["differs"], st["summary"])

        # ---- edit offline
        body, hdr = multipart("New Clip", anm(5, 3000))
        c, r = req(ed, "POST", "/api/anims?name=New%20Clip", raw=body, headers=hdr)
        check("add an animation to the workspace", c == 200 and any(a["id"] == "new-clip" for a in r["anims"]), r)
        c, r = req(ed, "POST", "/api/sets", {"name": "Long", "frames": [fr(("W" * 300, "", ""))]})
        check("a text that does not fit is refused", c == 400 and "top" in r.get("error", ""), r)
        c, r = req(ed, "POST", "/api/sets", {"name": "Bad", "frames": [fr(g=("u:nope", "", ""))]})
        check("a missing animation is refused", c == 400 and "animation" in r.get("error", ""), r)
        c, r = req(ed, "POST", "/api/sets", {"name": "New", "bpm": 90, "frames": [fr(g=("u:new-clip", "", "")), fr(("שלום", "", ""), mp3=2)]})
        check("add a set using the new animation", c == 200 and len(r["sets"]) == 3, r)
        new0 = dict(sets[0], frames=[fr(("STOP!", "המתן", "GO"), mp3=1)] + sets[0]["frames"][1:])
        c, r = req(ed, "PUT", "/api/sets/0", new0)
        check("edit set 1", c == 200 and r["sets"][0]["frames"][0]["t"][0] == "STOP!")
        req(ed, "DELETE", "/api/anims/skyroads")
        req(ed, "POST", "/api/settings", {"offDark": True})
        req(ed, "POST", "/api/mp3/volume", {"volume": 25})
        c, r = req(ed, "POST", "/api/anims/froggy/rename", {"name": "frog"})
        check("rename an animation: sets follow", c == 200 and req(ed, "GET", "/api/state")[1]["sets"][0]["frames"][1]["g"][1] == "u:frog", r)
        S = req(ed, "GET", "/api/editor/status")[1]["summary"]
        check("summary lists the changes", S["differs"] and S["animsAdded"] == ["frog", "new-clip"] and S["animsOnlyOnSign"] == ["froggy", "skyroads"]
              and S["sets"]["changed"] == ["Morning"] and S["sets"]["added"] == ["New"] and set(S["settings"]) == {"offDark", "volume"}, S)
        check("the sign is untouched so far", sl.canon_sets(req(sign, "GET", "/api/state")[1]["sets"]) == sl.canon_sets(sets))

        # ---- compare and plan
        req(ed, "POST", "/api/editor/check", {"target": sign, "deleteRemoved": False})
        j = wait_job(ed)
        p = j.get("result", {}).get("plan", {})
        ops = [(x["op"], x.get("id")) for x in p.get("steps", [])]
        check("plan without deletes", j["state"] == "done" and ops == [("add", "new-clip"), ("add", "frog"), ("sets", None), ("settings", None)], ops)
        c, r = req(ed, "POST", "/api/editor/plan", {"deleteRemoved": True})
        ops = [(x["op"], x.get("id")) for x in r["plan"]["steps"]]
        check("plan with deletes: unused first, used after the sets", ops == [("delete", "skyroads"), ("add", "new-clip"), ("add", "frog"), ("sets", None), ("delete", "froggy"), ("settings", None)], ops)

        # ---- upload
        req(ed, "POST", "/api/editor/upload", {"target": sign, "deleteRemoved": True})
        j = wait_job(ed)
        check("upload finished", j["state"] == "done", j)
        s2 = req(sign, "GET", "/api/state")[1]
        mine = req(ed, "GET", "/api/state")[1]
        check("the sign's sets = the workspace's", sl.canon_sets(s2["sets"]) == sl.canon_sets(mine["sets"]))
        check("the sign's animations = the workspace's", sorted(a["id"] for a in s2["anims"]) == sorted(a["id"] for a in mine["anims"]) == ["car-race", "frog", "new-clip"], s2["anims"])
        check("the sign's settings and volume", s2["settings"]["offDark"] is True and s2["mp3"]["volume"] == 25, s2["settings"])
        S = req(ed, "GET", "/api/editor/status")[1]["summary"]
        check("after upload: same as the sign", not S["differs"], S)
        req(ed, "POST", "/api/editor/check", {"target": sign})
        j = wait_job(ed)
        check("compare again: nothing to do", j["result"]["plan"]["nothing"], j["result"]["plan"]["steps"])

        # ---- the phone changed a set on the sign: the plan says so
        req(sign, "PUT", "/api/sets/1", {"name": "Race", "bpm": 100, "frames": [fr(("PHONE", "", ""))]})
        req(ed, "POST", "/api/editor/check", {"target": sign})
        p = wait_job(ed)["result"]["plan"]
        check("a set changed from the phone is noticed", any("phone" in w for w in p["warnings"]) and [x["op"] for x in p["steps"]] == ["sets"], p)

        # ---- a download that broke off goes on from where it was
        part = os.path.join(wsd, "incoming")
        os.makedirs(part, exist_ok=True)
        shutil.copy(os.path.join(wsd, "anims", "car-race.anm"), os.path.join(part, "car-race.anm"))
        req(ed, "POST", "/api/editor/download", {"target": sign, "refetch": True})
        j = wait_job(ed)
        check("resumed download keeps a file already fetched", j["state"] == "done" and any("car-race: already fetched" in x for x in j["log"]), j.get("log"))
        check("download took the phone's edit", req(ed, "GET", "/api/state")[1]["sets"][1]["frames"][0]["t"][0] == "PHONE")
        check("incoming folder cleared", not os.path.exists(part))

        # ---- music folder
        c, r = req(ed, "POST", "/api/editor/music", {"dir": music})
        st = req(ed, "GET", "/api/editor/status")[1]
        check("music folder: mp3 and wav in name order", [t["name"] for t in st["music"]["tracks"]] == ["A.mp3", "b.mp3", "c.wav"], st["music"])
        check("state reports the tracks", req(ed, "GET", "/api/state")[1]["mp3"]["tracks"] == 3)
        b2 = urllib.request.urlopen(ed + "/music/2").read()
        check("/music/2 = the second file", b2 == open(os.path.join(music, "b.mp3"), "rb").read())
        rq = urllib.request.Request(ed + "/music/1", headers={"Range": "bytes=3-9"})
        with urllib.request.urlopen(rq) as resp:
            check("range request", resp.status == 206 and resp.read() == open(os.path.join(music, "A.mp3"), "rb").read()[3:10])

        # ---- the text engine
        c, rle = req(ed, "GET", "/api/preview.rle?t0=STOP&t1=%D7%94%D7%9E%D7%AA%D7%9F&t2=GO&m=7&k=0")
        check("preview.rle from the text engine", isinstance(rle, bytes) and rle[:2] == b"RL")
        c, r = req(ed, "GET", "/api/check?text=STOP&light=0&size=0")
        check("check from the text engine", r.get("ok") is True and r.get("size", 0) > 0, r)
        for q in ("t0=STOP&t1=%D7%94%D7%9E%D7%AA%D7%9F&t2=GO&m=7&k=0&o=0", "g0=dancing&k=440&t1=Hello%20world&s1=22&c2=0&f2=0&t2=x&m=5&o=1",
                  "g0=fx:quarters&q0=1,2,3&g1=fx:pulse&hc2=60&t2=GO&bpm=120&k=1300&o=0"):
            mine, theirs = req(ed, "GET", "/api/preview.rle?" + q)[1], req(sign, "GET", "/api/preview.rle?" + q)[1]
            check(f"preview.rle as the harness's stopcli ({q[:20]}...)", mine == theirs)
        for t in ("STOP", "%D7%99%D7%95%D7%9D%20%D7%94%D7%95%D7%9C%D7%93%D7%AA%20%D7%A9%D7%9E%D7%97", "a%20b%20c%20d%20e%20f%20g%20h"):
            q = f"/api/check?text={t}&light=1&size=30"
            check(f"check as the harness's stopcli ({t[:12]})", req(ed, "GET", q)[1] == req(sign, "GET", q)[1])
        studio_endpoints(ed, wsd)
    finally:
        for p in procs:
            p.terminate()
        for p in procs:
            p.wait(5)
        if FAILS:
            print("  editor log:", open(os.path.join(tmp, "editor.log")).read()[-1500:])
        shutil.rmtree(tmp, ignore_errors=True)


def e2e_serial(loss):
    """Download / upload over the cable, against the firmware's own
    SerialLink.cpp built for the PC (tools/seriallink_test.py), in-process."""
    print(f"--- USB cable round trip with the simulated board ({'loss %d%%' % (loss * 100) if loss else 'clean link'})")
    import seriallink_test as st
    st.build()
    tmp = tempfile.mkdtemp(prefix="edser-")
    root, wsd = os.path.join(tmp, "board"), os.path.join(tmp, "ws")
    os.makedirs(os.path.join(root, "a"))
    files = {"froggy": anm(3, 5000), "car-race": anm(2, 2000), "skyroads": anm(4, 100)}
    for k, v in files.items():
        open(os.path.join(root, "a", k + ".anm"), "wb").write(v)
    link = st.SimLink(root, loss, seed=3)
    real_open = sl.open_board
    sl.open_board = lambda target, link_=None: sl.SerialBoard(link=link)
    try:
        sets = [{"name": "Morning", "bpm": 120, "frames": [fr(("STOP", "המתן", "GO"), mp3=1), fr(g=("", "u:froggy", ""))], "loops": [{"from": 1, "to": 2, "count": 3}]},
                {"name": "Race", "frames": [fr(g=("u:car-race", "", "fx:pulse"))]}]
        sl.SerialBoard(link=link).put_sets(sets)
        ws = se.Workspace(wsd)
        jobs = se.Jobs()
        r = se.do_download(ws, "sim", False, jobs)
        check("cable download", r["anims"] == 3 and sl.canon_sets(ws.data["sets"]) == sl.canon_sets(sets), r)
        check("cable download: files byte for byte", all(open(ws.anim_path(k), "rb").read() == v for k, v in files.items()))
        check("cable download: settings", ws.data["settings"]["rotate180"] is True and ws.data["volume"] == 20, ws.data)
        check("same as the sign", not se.summary(ws)["differs"], se.summary(ws))
        open(ws.anim_path("new-clip"), "wb").write(anm(6, 7000))
        open(ws.anim_path("froggy"), "wb").write(anm(3, 6000))   # changed here
        os.remove(ws.anim_path("skyroads"))
        ws.data["sets"].append({"name": "New", "frames": [fr(g=("u:new-clip", "", ""), mp3=-2, fade=2.5)]})
        ws.data["sets"][0]["frames"][0]["t"][0] = "STOP!"
        ws.data["volume"], ws.data["settings"]["offDark"] = 9, True
        ws.save()
        p = se.do_check(ws, "sim", True, jobs)["plan"]
        ops = [(x["op"], x.get("id")) for x in p["steps"]]
        check("cable plan (the board's checksums find the changed clip)", ops == [("delete", "skyroads"), ("replace", "froggy"), ("add", "new-clip"), ("sets", None), ("settings", None)], ops)
        se.do_upload(ws, "sim", True, jobs)
        b = sl.SerialBoard(link=link)
        info = b.info()
        mine = ws.anim_info()
        check("cable upload: animations", sorted(a["id"] for a in info["anims"]) == sorted(mine), info)
        check("cable upload: checksums", b.sums(sorted(mine)) == {k: v["crc"] for k, v in sorted(mine.items())})
        check("cable upload: sets", b.sets() == sl.canon_sets(ws.data["sets"]))
        conf = b.settings()
        check("cable upload: settings", conf["volume"] == 9 and conf["offDark"] is True, conf)
        p = se.do_check(ws, "sim", True, jobs)["plan"]
        check("cable: nothing left to do", p["nothing"], p["steps"])
    except Exception as e:
        import traceback
        traceback.print_exc()
        check(f"cable round trip without exception ({type(e).__name__})", False, e)
    finally:
        sl.open_board = real_open
        link.close()
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    if "--studio" in sys.argv:   # Windows Python: just the card's conversion
        studio_convert()
        print("FAILED: " + ", ".join(FAILS) if FAILS else "all passed")
        sys.exit(1 if FAILS else 0)
    plan_units()
    space_units()
    studio_convert()
    e2e()
    e2e_serial(0.0)
    e2e_serial(0.02)
    print("FAILED: " + ", ".join(FAILS) if FAILS else "all passed")
    sys.exit(1 if FAILS else 0)
