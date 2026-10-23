#!/usr/bin/env python3
"""Talk to the sign for tools/sign_editor.py, over WiFi (the HTTP API of
src/WebUI.cpp) or a USB cable (the "#ANIM" lines of src/SerialLink.h).

    board = open_board("http://192.168.4.1")   # or "COM19", or "usb" = the first sign port
    board.info()        {"anims": [{id, full, frames, ms, bytes}], "free", "total"}
    board.sets()        the stored sets (JSON as the firmware writes it)
    board.settings()    {offDark, rotate180, colourRotated, volume, mp3, tracks}
    board.sums(ids)     {id: crc32} over the cable ({} over WiFi: no checksums there)
    board.fetch_anim(id, size, progress) / put_anim(id, data, progress) / del_anim(id)
    board.put_sets(sets, progress)        replaces every stored set
    board.put_settings(want, have)        only the keys that differ

Every call raises BoardError(why) on failure. canon_set() mirrors
setFromJson + setToJson (src/SetStore.cpp) so a set can be compared with what
the board stores; keep it in step with that file.
"""
import json, os, struct, sys, time, urllib.error, urllib.parse, urllib.request, zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import anim_convert as ac   # serial_link, serial_reply, serial_put, fetch_serial, upload_http, sign_ports


class BoardError(RuntimeError):
    pass


# ---------------------------------------------------------------- canonical sets
DEF_BG = [48, 52, 12]
DEF_Q = [60, 12, 3]
MAX_SETS, MAX_FRAMES, MAX_LOOPS, MAX_JSON_BYTES = 40, 64, 16, 32000


def _int(v, default):
    """ArduinoJson's `v | default` for an integer: the value if it is an integer."""
    return v if isinstance(v, int) and not isinstance(v, bool) else default


def _num(v, default):
    return v if isinstance(v, (int, float)) and not isinstance(v, bool) else default


def _str(v, default=""):
    return v if isinstance(v, str) else default


def _at(o, key, i):
    a = o.get(key)
    return a[i] if isinstance(a, list) and i < len(a) else None


def _cmod(a, b):
    """C's % (the sign follows the dividend)."""
    return a - b * int(a / b)


def _f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def _cut_bytes(s, n):
    return s.encode("utf-8")[:n].decode("utf-8", "ignore")


def canon_frame(o):
    lights = []
    for i in range(3):
        text = _str(_at(o, "t", i))
        kind = _str(_at(o, "k", i), "long" if "\n" in text else "short")
        g = _str(_at(o, "g", i))
        if kind == "off":
            text, g = "", ""
        gm = _int(_at(o, "gm", i), 0) & 0xFF
        af, al = _int(_at(o, "af", i), 0) & 0xFFFFFFFF, _int(_at(o, "al", i), 0) & 0xFFFFFFFF
        bg, ink = _int(_at(o, "c", i), DEF_BG[i]) & 0xFF, _int(_at(o, "f", i), 0) & 0xFF
        qa = o.get("q")
        qi = qa[i] if isinstance(qa, list) and i < len(qa) and isinstance(qa[i], list) else []
        q = []
        for k in range(3):
            v = _int(qi[k] if k < len(qi) else None, DEF_Q[k])
            q.append(DEF_Q[k] if v < 0 or v > 64 else v)
        lights.append({"t": text, "k": kind, "g": g, "on": kind != "off", "s": _int(_at(o, "s", i), 0) & 0xFF,
                       "gm": 0 if gm > 2 else gm, "af": 0 if af > 600000 else af, "al": 0 if al > 600000 else al,
                       "c": DEF_BG[i] if bg > 64 else bg, "f": 0 if ink > 64 else ink,
                       "hc": _cmod(_int(_at(o, "hc", i), 0), 360), "hf": _cmod(_int(_at(o, "hf", i), 0), 360), "q": q})
    if isinstance(o.get("m"), int) and not isinstance(o.get("k"), list):   # legacy: mask without kinds
        for i in range(3):
            lights[i]["on"] = bool((o["m"] >> i) & 1)
    out = {k: [L[k] for L in lights] for k in ("t", "k", "g", "s", "c", "f")}
    if any(L["af"] or L["al"] for L in lights):
        out["af"], out["al"] = [L["af"] for L in lights], [L["al"] for L in lights]
    if any(L["gm"] for L in lights):
        out["gm"] = [L["gm"] for L in lights]
    if any(L["hc"] or L["hf"] for L in lights):
        out["hc"], out["hf"] = [L["hc"] for L in lights], [L["hf"] for L in lights]
    if any(L["g"] == "fx:quarters" for L in lights):
        out["q"] = [L["q"] for L in lights]
    out["m"] = sum((1 << i) for i, L in enumerate(lights) if L["on"])
    ms = _int(o.get("ms"), 1000) & 0xFFFFFFFF
    out["ms"] = 100 if ms < 100 else 600000 if ms > 600000 else ms
    t = _int(o.get("mp3"), 0)
    t = -2 if t < -2 else 3000 if t > 3000 else t
    if t:
        out["mp3"] = t
    if t == -2:
        fs = _f32(_num(o.get("fade"), 3.0))
        fade_ms = 200 if fs < 0.2 else 60000 if fs > 60 else int(_f32(fs * 1000.0))
        # The board truncates to whole ms (0.7 s -> 699 ms) and reports a float:
        # compare in 10 ms steps so a set read back equals the one sent.
        out["fade"] = round(fade_ms / 1000.0, 2)
    return out


def canon_set(o):
    """The set as the board stores and reports it (src/SetStore.cpp)."""
    bpm = _int(o.get("bpm"), 120)
    out = {"name": _cut_bytes(_str(o.get("name")), 40), "bpm": 10 if bpm < 10 else 400 if bpm > 400 else bpm}
    if isinstance(o.get("frames"), list):
        frames = [canon_frame(f if isinstance(f, dict) else {}) for f in o["frames"][:MAX_FRAMES]]
    else:   # legacy single-frame set
        f = canon_frame(o)
        f["ms"] = 1000
        frames = [f]
    loops = []
    for L in o.get("loops") or []:
        if not isinstance(L, dict):
            continue
        a, b, c = _int(L.get("from"), 0) & 0xFFFF, _int(L.get("to"), 0) & 0xFFFF, _int(L.get("count"), 1) & 0xFFFF
        if a < 1 or b < a or b > len(frames) or c < 1:
            continue
        c = min(c, 1000)
        if any(not (b < x["from"] or a > x["to"]) for x in loops):
            continue
        loops.append({"from": a, "to": b, "count": c})
        if len(loops) >= MAX_LOOPS:
            break
    loops.sort(key=lambda x: x["from"])
    out["frames"], out["loops"] = frames, loops
    return out


def canon_sets(sets):
    return [canon_set(s) for s in sets]


def stored_bytes(sets):
    """About what the board's NVS blob will take (its limit is MAX_JSON_BYTES)."""
    return len(json.dumps({"sets": canon_sets(sets)}, ensure_ascii=False, separators=(",", ":")).encode("utf-8"))


def anims_used(sets):
    return sorted({g[2:] for s in sets for f in s.get("frames", []) for i, g in enumerate(f.get("g") or [])
                   if isinstance(g, str) and g.startswith("u:") and _str(_at(f, "k", i)) != "off"})


def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


# ---------------------------------------------------------------- WiFi
class WifiBoard:
    kind = "wifi"

    def __init__(self, base, timeout=8):
        self.base = base.rstrip("/")
        self.timeout = timeout
        self.label = self.base

    def _req(self, method, path, body=None, timeout=None, raw=False):
        data = json.dumps(body, ensure_ascii=False).encode("utf-8") if body is not None else None
        req = urllib.request.Request(self.base + path, data=data, method=method,
                                     headers={"Content-Type": "application/json"} if data is not None else {})
        try:
            with urllib.request.urlopen(req, timeout=timeout or self.timeout) as r:
                b = r.read()
        except urllib.error.HTTPError as e:
            b = e.read()
            try:
                why = json.loads(b).get("error")
            except Exception:
                why = None
            raise BoardError(why or f"HTTP {e.code} from {path}")
        except (urllib.error.URLError, OSError) as e:
            raise BoardError(f"cannot reach the sign at {self.base} ({getattr(e, 'reason', e)}). Join this PC to the StopLight WiFi first.")
        if raw:
            return b
        try:
            return json.loads(b or b"{}")
        except ValueError:
            raise BoardError(f"{self.base}{path} did not answer with JSON (is this the sign?)")

    def state(self):
        return self._req("GET", "/api/state")

    def info(self):
        j = self._req("GET", "/api/anims")
        return {"anims": j.get("anims", []), "free": j.get("free"), "total": j.get("total")}

    def sets(self):
        return self.state().get("sets", [])

    def settings(self):
        s = self.state()
        st, mp3 = s.get("settings", {}), s.get("mp3", {})
        return {"offDark": bool(st.get("offDark")), "rotate180": bool(st.get("rotate180")), "colourRotated": bool(st.get("colourRotated")),
                "volume": mp3.get("volume", 20), "mp3": bool(mp3.get("present")), "tracks": mp3.get("tracks", 0)}

    def sums(self, ids):
        return {}

    def fetch_anim(self, aid, size, progress=None):
        try:
            with urllib.request.urlopen(f"{self.base}/api/anims/{urllib.parse.quote(aid)}.anm", timeout=30) as r:
                out = bytearray()
                while True:
                    b = r.read(16384)
                    if not b:
                        break
                    out += b
                    if progress:
                        progress(len(out), size)
        except urllib.error.HTTPError as e:
            raise BoardError(f"{aid}: HTTP {e.code}")
        except (urllib.error.URLError, OSError) as e:
            raise BoardError(f"{aid}: download broke off ({getattr(e, 'reason', e)})")
        if size is not None and len(out) != size:
            raise BoardError(f"{aid}: got {len(out)} of {size} bytes")
        return bytes(out)

    def put_anim(self, aid, data, progress=None):
        res = ac.upload_http(self.base, aid, data, (lambda ph, d, t, u="": progress(d, t)) if progress else None)
        if "error" in res:
            raise BoardError(res["error"])
        return res.get("free")

    def del_anim(self, aid):
        return self._req("DELETE", f"/api/anims/{urllib.parse.quote(aid)}").get("free")

    def put_sets(self, sets, progress=None):
        """No bulk call over WiFi: delete the extra sets (from the end), replace
        those that differ, add the missing ones. Each call is checked by the board
        like the page's Add / Save; a failure stops with the board's reason, and a
        retry only redoes what still differs."""
        have = self.sets()
        want = canon_sets(sets)
        n_ops = max(0, len(have) - len(want)) + sum(1 for i, s in enumerate(want) if i >= len(have) or canon_set(have[i]) != s)
        done = 0
        def tick():
            nonlocal done
            done += 1
            if progress:
                progress(done, max(1, n_ops))
        for i in range(len(have) - 1, len(want) - 1, -1):
            self._req("DELETE", f"/api/sets/{i}")
            tick()
        for i, s in enumerate(want):
            name = f"set {i + 1}" + (f" ({s['name']})" if s["name"] else "")
            try:
                if i < len(have):
                    if canon_set(have[i]) != s:
                        self._req("PUT", f"/api/sets/{i}", sets[i], timeout=60)
                        tick()
                else:
                    self._req("POST", "/api/sets", sets[i], timeout=60)
                    tick()
            except BoardError as e:
                raise BoardError(f"{name}: {e}")
        return len(want)

    def put_settings(self, want, have):
        body = {k: bool(want[k]) for k in ("offDark", "rotate180") if k in want and bool(want[k]) != bool(have.get(k))}
        if body:
            self._req("POST", "/api/settings", body)
        if "volume" in want and int(want["volume"]) != int(have.get("volume", -1)):
            self._req("POST", "/api/mp3/volume", {"volume": int(want["volume"])})
        if "colourRotated" in want and bool(want["colourRotated"]) != bool(have.get("colourRotated")):
            try:   # the board saves it and restarts; the reply may not arrive
                self._req("POST", "/api/settings", {"colourRotated": bool(want["colourRotated"])}, timeout=5)
            except BoardError:
                pass
            end = time.time() + 30
            time.sleep(4)
            while time.time() < end:
                try:
                    self.state()
                    return True
                except BoardError:
                    time.sleep(1)
            raise BoardError("the sign restarted for the colour wiring setting and has not come back on WiFi yet")
        return False

    def close(self):
        pass


# ---------------------------------------------------------------- USB cable
class SerialBoard:
    kind = "usb"

    def __init__(self, port="usb", link=None):
        self.port = port
        self._given = link is not None
        self.s = link if link is not None else self._open()
        self.label = self.port if not self._given else "test link"

    def _open(self):
        port = self.port
        if port in ("", "usb", "auto", None):
            ports = ac.sign_ports()
            if not ports:
                raise BoardError("no sign found on a USB port")
            port = self.port = ports[0][0]
        try:
            s = ac.serial_link(port)
        except Exception as e:
            raise BoardError(f"cannot open {port}: {e}")
        s.reset_input_buffer()
        return s

    def reopen(self):
        """After an error: the port may have gone (cable pulled, board restarted)."""
        if self._given:
            return
        try:
            self.s.close()
        except Exception:
            pass
        last = None
        for _ in range(10):
            try:
                self.s = self._open()
                return
            except BoardError as e:
                last = e
                time.sleep(1)
        raise last

    def ask(self, cmd, want, timeout=2.0, tries=3):
        """Send '#ANIM <cmd>' and return the rest of the first reply starting with
        want; replies to earlier commands are skipped, a lost reply is asked again."""
        garbled = None
        for _ in range(tries):
            self.s.write(f"\n#ANIM {cmd}\n".encode("utf-8"))
            end = time.time() + timeout
            while True:
                r = ac.serial_reply(self.s, max(0.05, end - time.time()))
                if r is None:
                    break
                if r.startswith(want):
                    return r[len(want):]
                if r in ("ERR unknown command", "ERR bad request"):
                    garbled = r[4:]   # the line may have lost a byte on the way: send it again
                    break
                if r.startswith("ERR "):
                    raise BoardError(r[4:])
        raise BoardError(garbled or f"no answer from the sign to {cmd.split()[0]} (is the firmware with the editor commands on it?)")

    def info(self):
        def once():
            anims = json.loads(self.ask("LIST", "LIST "))
            free, total = (int(x) for x in self.ask("SPACE", "SPACE ").split()[:2])
            return {"anims": anims, "free": free, "total": total}
        return self._retry(once)

    TRANSIENT = ("no answer", "keeps failing", "incomplete", "checksum", "no upload open", "timeout", "cannot open",
                 "more data than announced", "no snapshot", "bad request", "Expecting", "Unterminated", "codec")

    def _retry(self, fn, tries=3):
        """Run fn, again after a link failure (a lost line, a dropped reply, a
        replugged cable). A refusal by the board (a text that does not fit,
        storage full...) is not retried."""
        for i in range(tries):
            try:
                return fn()
            except (BoardError, RuntimeError, OSError, ValueError, IndexError) as e:
                garbled = isinstance(e, (ValueError, IndexError))   # a reply that lost a byte on the way
                if i == tries - 1 or not (garbled or any(t in str(e) for t in self.TRANSIENT + ("Errno", "device"))):
                    raise BoardError(str(e))
                try:
                    self.reopen()
                    self.s.write(b"\n")
                    time.sleep(0.3)
                    self.s.reset_input_buffer()
                    self.s._anim_buf = b""
                except Exception:
                    pass

    def sets(self):
        def once():
            size, crc = self.ask("SETS", "SETS ").split()[:2]
            data = ac.fetch_serial(self.s, ".sets", int(size))
            if crc32(data) != int(crc, 16):
                raise BoardError("the sets read back failed their checksum")
            return json.loads(data.decode("utf-8")).get("sets", [])
        return self._retry(once)

    def settings(self):
        return self._retry(lambda: json.loads(self.ask("CONF", "CONF ")))

    def sums(self, ids):
        out = {}
        for aid in ids:
            try:
                r = self.ask(f"SUM {aid}", f"SUM {aid} ", timeout=4).split()
            except BoardError as e:
                if "unknown command" in str(e):
                    return {}   # firmware without SUM: compare sizes only
                raise
            try:
                out[aid] = int(r[1], 16)
            except (ValueError, IndexError):
                out[aid] = None   # garbled reply: treat as unknown (the plan then uploads it)
        return out

    def fetch_anim(self, aid, size, progress=None):
        return self._retry(lambda: ac.fetch_serial(self.s, aid, size, progress))

    def put_anim(self, aid, data, progress=None):
        return self._retry(lambda: int(ac.serial_put(self.s, f"PUT {aid} {len(data)}", data, progress).split()[0]))

    def del_anim(self, aid):
        return int(self.ask(f"DEL {aid}", "OK ", timeout=8).split()[0])

    def put_sets(self, sets, progress=None):
        body = json.dumps({"sets": sets}, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        # The board checks every text before it answers END: allow time.
        return self._retry(lambda: int(ac.serial_put(self.s, f"SPUT {len(body)}", body, progress, end_timeout=120).split()[0]))

    def put_settings(self, want, have):
        body = {k: bool(want[k]) for k in ("offDark", "rotate180") if k in want and bool(want[k]) != bool(have.get(k))}
        if "volume" in want and int(want["volume"]) != int(have.get("volume", -1)):
            body["volume"] = int(want["volume"])
        if body:
            self.ask("CONF " + json.dumps(body, separators=(",", ":")), "CONF ")
        if "colourRotated" in want and bool(want["colourRotated"]) != bool(have.get("colourRotated")):
            self.ask("CONF " + json.dumps({"colourRotated": bool(want["colourRotated"])}, separators=(",", ":")), "CONF ")
            time.sleep(5)   # the board restarts
            self.reopen()
            return True
        return False

    def close(self):
        if not self._given:
            try:
                self.s.close()
            except Exception:
                pass


def open_board(target, link=None):
    if target.startswith("http"):
        return WifiBoard(target)
    return SerialBoard(target, link)


def sign_ports():
    try:
        return ac.sign_ports()
    except Exception:   # no pyserial here (WSL python): WiFi only
        return []
