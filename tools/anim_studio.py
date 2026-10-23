#!/usr/bin/env python3
"""StopLight animation studio: a local web app that turns a GIF, a video file
or a YouTube link into a sign animation (.anm), with a crop box, in/out
times, a preview of what the LEDs will show, and upload to the sign.

    python.exe tools/anim_studio.py        # then open http://localhost:8090/

The same studio is also a card in the offline editor (tools/sign_editor.py),
which converts into its workspace instead of uploading. Both use this module:
    Studio      the server side (open a file or a link, rotate, convert, the
                progress of each, the preview video), mounted under a path prefix
    fragment()  the page side (source, crop and trim, output, preview), with a
                slot for what the host does with the result
    deps()      which optional packages are there

Run it with Windows Python: it uses ffmpeg (imageio-ffmpeg), yt-dlp and numpy
from there, and only Windows can reach the sign's WiFi (USB dongle joined to
'StopLight'). Sources are kept in tools/gfx/sources/, results in
tools/gfx/<name>.anm. The conversion itself is anim_convert.py. Importing
this module needs only numpy: ffmpeg and yt-dlp are looked up when used.
"""
import base64, importlib.util, json, os, re, shutil, subprocess, sys, tempfile, threading, urllib.parse, urllib.request, uuid, webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import anim_convert as ac   # noqa: E402  (numpy only at import time)

HERE = os.path.dirname(os.path.abspath(__file__))
GFX = os.path.join(HERE, "gfx")
SRC = os.path.join(GFX, "sources")
WORK = os.path.join(GFX, "work")       # converted, not yet named / uploaded
PORT = 8090


# ======================================================================= server side
def deps():
    """Which optional packages the studio can use. ffmpeg (imageio-ffmpeg, or an
    ffmpeg on PATH) reads every source; yt-dlp fetches links."""
    ff = importlib.util.find_spec("imageio_ffmpeg") is not None or shutil.which("ffmpeg") is not None
    yt = importlib.util.find_spec("yt_dlp") is not None
    missing = [p for p, ok in (("imageio-ffmpeg", ff), ("yt-dlp", yt)) if not ok]
    py = os.path.basename(sys.executable or "python") or "python"
    return {"ffmpeg": ff, "ytdlp": yt, "missing": missing,
            "install": f"{py} -m pip install {' '.join(missing)}" if missing else ""}


def make_proxy(src, out, progress=None, rotate=0):
    """An H.264 copy the browser can play and scrub (GIFs and odd codecs included)."""
    total = ac.probe_duration(src)
    cmd = [ac.ffmpeg_exe(), "-v", "error", "-y", "-i", src, "-vf", ac.ROTATE.get(int(rotate) % 360, "") + "scale='min(640,iw)':-2,format=yuv420p",
           "-c:v", "libx264", "-preset", "veryfast", "-crf", "23", "-an", "-movflags", "+faststart",
           "-progress", "pipe:1", "-nostats", out]
    with tempfile.TemporaryFile() as errf:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=errf, text=True)
        for line in proc.stdout:
            if progress and line.startswith("out_time_us=") and line[12:].strip().isdigit():
                progress("prepare", round(int(line[12:]) / 1e6, 1), round(total, 1), "s")
        proc.wait()
        errf.seek(0)
        err = errf.read().decode(errors="replace")
    if proc.returncode != 0:
        raise RuntimeError("ffmpeg could not read this file: " + err[-300:])


class Studio:
    """The studio's server side, mounted under `prefix` of a host's HTTP handler:
        GET  <prefix>/progress?t=<token>   {phase, done, total, unit} of the request carrying X-Token
        GET  <prefix>/deps                 deps()
        GET  <prefix>/media/<id>.mp4       the preview video (range requests)
        POST <prefix>/open-file            body = the file (X-Filename)    -> {id, name}
        POST <prefix>/open-url             {url}                            -> {id, name}
        POST <prefix>/rotate               {id, rotate}                     -> {ok}
        POST <prefix>/convert              {id, rotate, shape, fps, start, duration, crop, dither, threshold, gain}
                                           -> {work, frames, distinct, bytes, format, anm (base64)}
    Sources go to src_dir (keep: only the newest few are kept), results to
    work_dir/<id>.anm. The host's handler needs _json(obj, code)."""

    def __init__(self, src_dir, work_dir, prefix="/api", keep=None):
        self.src_dir, self.work_dir, self.prefix, self.keep = src_dir, work_dir, prefix.rstrip("/"), keep
        self.progress = {}   # token -> {phase, done, total, unit}

    def reporter(self, token):
        def cb(phase, done, total, unit=""):
            if token:
                self.progress[token] = {"phase": phase, "done": done, "total": total, "unit": unit}
        return cb

    def source_path(self, sid):
        sid = re.sub(r"[^0-9a-f]", "", sid or "")
        if sid and os.path.isdir(self.src_dir):
            for fn in os.listdir(self.src_dir):
                if fn.startswith(sid + ".src"):
                    return os.path.join(self.src_dir, fn)
        return None

    def work_path(self, wid):
        wid = re.sub(r"[^0-9a-f]", "", wid or "")
        return os.path.join(self.work_dir, wid + ".anm") if wid else None

    def _new_source(self):
        os.makedirs(self.src_dir, exist_ok=True)
        if self.keep:   # sources are big: keep the newest few (and their converted clips)
            ids = {}
            for fn in os.listdir(self.src_dir):
                p = os.path.join(self.src_dir, fn)
                sid = fn.split(".")[0]
                ids[sid] = max(ids.get(sid, 0), os.path.getmtime(p))
            for sid in sorted(ids, key=ids.get)[:max(0, len(ids) - self.keep + 1)]:
                for d in (self.src_dir, self.work_dir):
                    if os.path.isdir(d):
                        for fn in os.listdir(d):
                            if fn.split(".")[0] == sid:
                                try:
                                    os.remove(os.path.join(d, fn))
                                except OSError:
                                    pass
        return uuid.uuid4().hex[:10]

    def _need(self, what="ffmpeg"):
        d = deps()
        if not d["ffmpeg"] or (what == "ytdlp" and not d["ytdlp"]):
            need = "imageio-ffmpeg" if not d["ffmpeg"] else "yt-dlp"
            raise LookupError(f"this needs the Python package {need}: {d['install']}")

    def handle_get(self, h, path):
        """True if path was the studio's (the reply is sent)."""
        u = urllib.parse.urlparse(path)
        if not u.path.startswith(self.prefix + "/"):
            return False
        what = u.path[len(self.prefix) + 1:]
        if what == "progress":
            t = urllib.parse.parse_qs(u.query).get("t", [""])[0]
            h._json(self.progress.get(t, {}))
            return True
        if what == "deps":
            h._json(deps())
            return True
        m = re.match(r"^media/([0-9a-f]+)\.mp4$", what)
        if m:
            self._media(h, os.path.join(self.src_dir, m.group(1) + ".mp4"))
            return True
        return False

    def _media(self, h, p):
        if not os.path.exists(p):
            return h.send_error(404)
        size = os.path.getsize(p)
        start, end = 0, size - 1
        rng = re.match(r"bytes=(\d*)-(\d*)", h.headers.get("Range", ""))
        if rng:
            if rng.group(1):
                start = int(rng.group(1))
            if rng.group(2):
                end = min(int(rng.group(2)), size - 1)
        with open(p, "rb") as f:
            f.seek(start)
            data = f.read(end - start + 1)
        h.send_response(206 if rng else 200)
        h.send_header("Content-Type", "video/mp4")
        h.send_header("Accept-Ranges", "bytes")
        h.send_header("Content-Length", str(len(data)))
        if rng:
            h.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        h.end_headers()
        h.wfile.write(data)

    def handle_post(self, h, path):
        """True if path was the studio's (the reply is sent). Reads the body itself."""
        u = urllib.parse.urlparse(path)
        if not u.path.startswith(self.prefix + "/"):
            return False
        what = u.path[len(self.prefix) + 1:]
        if what not in ("open-file", "open-url", "rotate", "convert"):
            return False
        body = h.rfile.read(int(h.headers.get("Content-Length", 0) or 0))
        cb = self.reporter(h.headers.get("X-Token", ""))
        try:
            h._json(*self._post(what, body, h.headers, cb))
        except LookupError as e:
            h._json({"error": str(e), "deps": deps()}, 400)
        except Exception as e:
            h._json({"error": str(e)}, 500)
        return True

    def _post(self, what, body, headers, cb):
        if what == "open-file":
            self._need()
            name = urllib.parse.unquote(headers.get("X-Filename", "clip"))
            sid = self._new_source()
            ext = re.sub(r"[^a-z0-9.]", "", os.path.splitext(name)[1].lower()) or ".bin"
            src = os.path.join(self.src_dir, sid + ".src" + ext)
            with open(src, "wb") as f:
                f.write(body)
            make_proxy(src, os.path.join(self.src_dir, sid + ".mp4"), cb)
            return {"id": sid, "name": os.path.splitext(os.path.basename(name))[0]}, 200
        o = json.loads(body or b"{}")
        if what == "open-url":
            self._need("ytdlp")
            url = (o.get("url") or "").strip()
            if not re.match(r"https?://", url):
                return {"error": "give a link that starts with http:// or https://"}, 400
            sid = self._new_source()
            with tempfile.TemporaryDirectory() as tmp:
                got = ac.download(url, tmp, 60, cb)
                src = os.path.join(self.src_dir, sid + ".src" + os.path.splitext(got)[1])
                shutil.move(got, src)
            make_proxy(src, os.path.join(self.src_dir, sid + ".mp4"), cb)
            return {"id": sid, "name": "clip"}, 200
        src = self.source_path(o.get("id"))
        if not src:
            return {"error": "source gone, open it again"}, 400
        self._need()
        if what == "rotate":   # remake the preview turned, so the crop box works on what you see
            make_proxy(src, os.path.join(self.src_dir, o["id"] + ".mp4"), cb, int(o.get("rotate", 0)))
            return {"ok": True}, 200
        # convert
        data, n, distinct = ac.convert(src, o.get("shape", "circle"), float(o.get("fps", 12)),
                                       float(o.get("start", 0)), float(o.get("duration", 0)), o.get("fit", "fill"),
                                       tuple(o["crop"]) if o.get("crop") else None, bool(o.get("dither")),
                                       float(o.get("threshold", 0.5)), float(o.get("gain", 1.0)),
                                       int(o.get("maxFrames", 300)), cb, True, int(o.get("rotate", 0)))
        os.makedirs(self.work_dir, exist_ok=True)
        with open(self.work_path(o["id"]), "wb") as f:   # named only when uploaded / added
            f.write(data)
        return {"work": re.sub(r"[^0-9a-f]", "", o["id"]), "frames": n, "distinct": distinct, "bytes": len(data), "format": data[3],
                "anm": base64.b64encode(data).decode()}, 200


# ======================================================================= page side
# fragment() is shared by the standalone page below and the offline editor's
# card. Everything is under .stu with ids starting "stu", and the script keeps
# its names to itself, so it sits next to the sign's page without clashes. It
# uses decodeV3() and inflateRaw() from the page (the sign's page has them; the
# standalone page loads tools/anim_v3.js). Hosts call
#   const S = AnimStudio({api: '/api', onOpened(j), onConverted(j)})
# and put their own section into #stuOut.
FRAGMENT_CSS = r"""<style>
.stu{display:grid;grid-template-columns:minmax(0,1fr) 300px;gap:16px}@media(max-width:900px){.stu{grid-template-columns:1fr}}
.stu .stu-sec{background:var(--panel);border:1px solid var(--rule);border-radius:8px;padding:14px;margin-bottom:16px}
.stu h3{font-size:13px;color:var(--muted);margin:0 0 10px;font-weight:600;text-transform:uppercase;letter-spacing:.08em}
.stu .row{display:flex;flex-wrap:wrap;gap:8px 14px;align-items:center;margin:8px 0}
.stu label{display:inline-flex;align-items:center;gap:6px;margin:0;font-size:14px}
.stu select{max-width:100%}
.stu input,.stu select,.stu button{font:inherit;font-size:14px;color:var(--ink);background:#111418;border:1px solid var(--rule);border-radius:6px;padding:5px 8px}
.stu input[type=number]{width:5em}.stu input[type=text]{width:auto;min-width:6em;flex:1;font-size:14px}.stu input[type=file]{max-width:100%}.stu input[type=color]{padding:1px 2px;width:3em;height:2em}
.stu button{cursor:pointer}.stu button.pri{background:var(--acc);color:#14171b;border-color:var(--acc);font-weight:600}.stu button:disabled{opacity:.5;cursor:default}
.stu .stage{position:relative;display:inline-block;max-width:100%;line-height:0}.stu video{max-width:100%;max-height:60vh;display:block;background:#000}
.stu #stuCrop{position:absolute;border:2px solid var(--acc);box-shadow:0 0 0 9999px rgba(0,0,0,.55);cursor:move;box-sizing:border-box;touch-action:none}
.stu #stuCrop i{position:absolute;right:-7px;bottom:-7px;width:14px;height:14px;background:var(--acc);border-radius:50%;cursor:nwse-resize}
.stu #stuCrop.circle::after{content:"";position:absolute;inset:0;border-radius:50%;border:1px dashed var(--acc);opacity:.8}
.stu .stage-wrap{overflow:hidden}
.stu .msg{color:var(--muted);font-size:13px;min-height:1.2em;margin-top:6px}.stu .err{color:var(--bad,#ff8a7a)}.stu .okc{color:var(--green,#8fe3a2)}
.stu canvas{background:#000;image-rendering:pixelated;display:block;margin:0 auto}
.stu .prog{margin:6px 0 0;font-size:13px;color:var(--muted)}.stu .prog[hidden]{display:none}.stu .prog .bar{height:8px;background:#0c0e11;border:1px solid var(--rule);border-radius:4px;overflow:hidden;margin-top:3px}
.stu .prog .bar i{display:block;height:100%;width:0;background:var(--acc);transition:width .25s}.stu .prog .bar.ind i{width:30%;animation:stuind 1.1s linear infinite}@keyframes stuind{from{margin-left:-30%}to{margin-left:100%}}
.stu .stbar{display:flex;height:14px;background:#0c0e11;border:1px solid var(--rule);border-radius:4px;overflow:hidden;margin:8px 0}
.stu .stbar i{flex:none;height:100%;box-shadow:inset -1px 0 #14171b}.stu .stbar i.fs{background:#3a4048}.stu .stbar i.gone{opacity:.3}
.stu .stbar i.new{background:repeating-linear-gradient(45deg,var(--acc) 0 4px,#9c5f1c 4px 8px)}.stu .stbar i.over{background:repeating-linear-gradient(45deg,#c0392b 0 4px,#6e1d15 4px 8px)}
.stu ul.stlist{list-style:none;margin:6px 0 0;padding:0;font-size:13px;max-height:260px;overflow:auto}
.stu ul.stlist li{display:grid;grid-template-columns:10px minmax(0,1fr) auto;gap:0 8px;align-items:center;padding:4px 0;border:0;border-top:1px solid var(--rule)}
.stu ul.stlist b{width:10px;height:10px;border-radius:2px}.stu ul.stlist .nm{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.stu ul.stlist .sz{font-variant-numeric:tabular-nums}.stu ul.stlist em{grid-column:2/4;color:var(--muted);font-style:normal;font-size:12px}
.stu ul.stlist li.new .nm{color:var(--acc)}
.stu .range{position:relative;height:28px}.stu .range input{position:absolute;inset:0;width:100%;pointer-events:none;background:none;border:0;padding:0}
.stu .range input::-webkit-slider-thumb{pointer-events:auto}.stu .range input::-moz-range-thumb{pointer-events:auto}
</style>"""

FRAGMENT_HTML = r"""<div class="stu" id="stu"><div>
 <section class="stu-sec"><h3>1 &middot; Source</h3>
  <div class="row"><input type="file" id="stuFile" accept="image/gif,video/*,image/webp"><span class="msg" style="margin:0">or</span><input type="text" id="stuUrl" placeholder="YouTube or other video link"><button id="stuOpenUrl">Open link</button></div>
  <div class="msg" id="stuSrcMsg">Pick a GIF or a video, or paste a link.</div><div class="msg err" id="stuDeps" hidden></div><div class="prog" id="stuSrcProg" hidden><span></span><div class="bar"><i></i></div></div></section>
 <section class="stu-sec" id="stuEdit" hidden><h3>2 &middot; Crop and trim</h3>
  <div class="stage-wrap"><div class="stage"><video id="stuV" muted loop playsinline controls></video><div id="stuCrop"><i></i></div></div></div>
  <div class="row"><label><input type="checkbox" id="stuLock" checked> keep the shape's proportions</label><label>Rotate <select id="stuRotate"><option value="0">none</option><option value="90">90&deg; right</option><option value="180">180&deg;</option><option value="270">90&deg; left</option></select></label><button id="stuCropAll">Whole picture</button><button id="stuCropFit">Largest crop</button><span class="msg" id="stuCropMsg" style="margin:0"></span></div>
  <div class="range"><input type="range" id="stuRin" min="0" max="1000" value="0"><input type="range" id="stuRout" min="0" max="1000" value="1000"></div>
  <div class="row"><span>In <input type="number" id="stuTin" step="0.1" min="0"> s <button id="stuSetIn">= now</button></span><span>Out <input type="number" id="stuTout" step="0.1" min="0"> s <button id="stuSetOut">= now</button></span><b id="stuLen"></b></div>
 </section></div>
 <div>
 <section class="stu-sec"><h3>3 &middot; Output</h3>
  <div class="row"><label>Shape <select id="stuShape"><option value="circle">one light (128&times;128)</option><option value="full">whole sign (128&times;384)</option></select></label></div>
  <div class="row"><label>Frames/s <input type="number" id="stuFps" value="12" min="1" max="30" step="1"></label><label><input type="checkbox" id="stuDither"> dither</label></div>
  <div class="row"><label>Brightness &times; <input type="number" id="stuGain" value="1" min="0.2" max="4" step="0.1"></label><label>Silhouette at <input type="number" id="stuThr" value="0.5" min="0.05" max="0.95" step="0.05"></label></div>
  <div class="row"><button class="pri" id="stuConvert" disabled>Convert</button></div><div class="msg" id="stuConvMsg"></div><div class="prog" id="stuConvProg" hidden><span></span><div class="bar"><i></i></div></div></section>
 <section class="stu-sec"><h3>4 &middot; Preview</h3>
  <div class="row"><select id="stuMode"><option value="0">original colours</option><option value="1">silhouette</option><option value="2">silhouette inverted</option></select><input type="color" id="stuFg" value="#ffffff" title="silhouette colour"><input type="color" id="stuBg" value="#ff0000" title="background colour"></div>
  <canvas id="stuPv" width="128" height="128" style="width:192px"></canvas></section>
 <div id="stuOut"></div>
 </div></div>"""

FRAGMENT_JS = r"""<script>
window.AnimStudio=function(o){
const $=id=>document.getElementById(id),API=o.api,v=$('stuV'),cropEl=$('stuCrop');
const S={src:null,work:null,bytes:0,format:0,frames:0,anim:null,deps:null};
let crop=[0,0,1,1],animT0=0,rot=0;
const LV=[0,140,195,255];
const aspect=()=>$('stuShape').value==='full'?128/384:1;   /* target width / height in pixels */
function vw(){return v.videoWidth||1}function vh(){return v.videoHeight||1}
function fitCrop(){const a=aspect(),srcA=vw()/vh();let w=1,h=1;if(srcA>a){w=a/srcA}else{h=srcA/a}crop=[(1-w)/2,(1-h)/2,w,h];drawCrop()}
function drawCrop(){const W=v.clientWidth,H=v.clientHeight;cropEl.style.left=crop[0]*W+'px';cropEl.style.top=crop[1]*H+'px';cropEl.style.width=crop[2]*W+'px';cropEl.style.height=crop[3]*H+'px';
 cropEl.classList.toggle('circle',$('stuShape').value==='circle');$('stuCropMsg').textContent=`${Math.round(crop[2]*vw())}×${Math.round(crop[3]*vh())} px of ${vw()}×${vh()}`}
function clamp(){crop[2]=Math.min(Math.max(crop[2],0.02),1);crop[3]=Math.min(Math.max(crop[3],0.02),1);crop[0]=Math.min(Math.max(crop[0],0),1-crop[2]);crop[1]=Math.min(Math.max(crop[1],0),1-crop[3])}
let drag=null;
cropEl.addEventListener('pointerdown',e=>{e.preventDefault();cropEl.setPointerCapture(e.pointerId);drag={resize:e.target.tagName==='I',x:e.clientX,y:e.clientY,c:crop.slice()}});
cropEl.addEventListener('pointermove',e=>{if(!drag)return;const dx=(e.clientX-drag.x)/v.clientWidth,dy=(e.clientY-drag.y)/v.clientHeight;
 if(drag.resize){let w=drag.c[2]+dx,h=drag.c[3]+dy;if($('stuLock').checked){const a=aspect()*vh()/vw();h=w/a;if(drag.c[1]+h>1){h=1-drag.c[1];w=h*a}if(drag.c[0]+w>1){w=1-drag.c[0];h=w/a}}crop=[drag.c[0],drag.c[1],w,h]}
 else crop=[drag.c[0]+dx,drag.c[1]+dy,drag.c[2],drag.c[3]];clamp();drawCrop()});
cropEl.addEventListener('pointerup',()=>drag=null);
$('stuCropAll').onclick=()=>{crop=[0,0,1,1];$('stuLock').checked=false;drawCrop()};$('stuCropFit').onclick=()=>{$('stuLock').checked=true;fitCrop()};
$('stuShape').onchange=()=>{if($('stuLock').checked)fitCrop();else drawCrop()};
window.addEventListener('resize',drawCrop);
/* in / out */
const dur=()=>v.duration&&isFinite(v.duration)?v.duration:0;
function setTimes(a,b){const d=dur();a=Math.max(0,Math.min(a,d));b=Math.max(a+0.05,Math.min(b,d));$('stuTin').value=a.toFixed(2);$('stuTout').value=b.toFixed(2);$('stuRin').value=d?a/d*1000:0;$('stuRout').value=d?b/d*1000:1000;
 const L=b-a,frames=Math.ceil(L*(+$('stuFps').value||12));$('stuLen').textContent=`${L.toFixed(2)} s · ${frames} frames`}
const tin=()=>+$('stuTin').value||0,tout=()=>+$('stuTout').value||dur();
$('stuRin').oninput=()=>{setTimes($('stuRin').value/1000*dur(),tout());v.currentTime=tin()};$('stuRout').oninput=()=>{setTimes(tin(),$('stuRout').value/1000*dur());v.currentTime=Math.max(tin(),tout()-0.3)};
$('stuTin').onchange=()=>setTimes(tin(),tout());$('stuTout').onchange=()=>setTimes(tin(),tout());$('stuFps').onchange=()=>setTimes(tin(),tout());
$('stuSetIn').onclick=()=>setTimes(v.currentTime,tout());$('stuSetOut').onclick=()=>setTimes(tin(),v.currentTime);
v.addEventListener('timeupdate',()=>{if(v.currentTime>=tout()||v.currentTime<tin()-0.05)v.currentTime=tin()});
v.addEventListener('loadedmetadata',()=>{$('stuEdit').hidden=false;setTimeout(()=>{fitCrop();setTimes(0,Math.min(dur(),6))},50);v.play().catch(()=>{});$('stuConvert').disabled=!(S.deps&&S.deps.ffmpeg)});
/* progress: the server records it per token while a request runs */
const PH={download:'Downloading',prepare:'Preparing the preview',decode:'Reading frames',colours:'Converting colours',encode:'Compressing',upload:'Uploading'};
function fmt(v,u){return u==='bytes'?(v/1048576).toFixed(v<1048576?2:1)+' MB':u==='s'?v.toFixed(1)+' s':v+(u?' '+u:'')}
async function withProgress(box,req){const token=Math.random().toString(36).slice(2),el=$(box),bar=el.querySelector('.bar'),fill=bar.querySelector('i'),lab=el.querySelector('span');
 el.hidden=false;bar.classList.add('ind');fill.style.width='';lab.textContent='Starting…';let done=false;
 const poll=async()=>{while(!done){const p=await fetch(API+'/progress?t='+token).then(r=>r.json()).catch(()=>({}));
  if(p.phase&&!done){const pct=p.total?Math.min(100,Math.round(100*p.done/p.total)):null;bar.classList.toggle('ind',pct===null);if(pct!==null)fill.style.width=pct+'%';
   lab.textContent=`${PH[p.phase]||p.phase}`+(p.total||p.done?`: ${fmt(p.done,p.unit)}${p.total?' of '+fmt(p.total,p.unit)+' ('+pct+'%)':''}`:'…')}
  await new Promise(r=>setTimeout(r,300))}};poll();
 try{return await req(token)}finally{done=true;el.hidden=true}}
const post=(path,t,body,hdr)=>fetch(API+path,{method:'POST',headers:Object.assign({'X-Token':t},hdr||{}),body}).then(r=>r.json()).catch(e=>({error:String(e)}));
const errTo=(el,msg)=>{el.innerHTML='<span class="err"></span>';el.firstChild.textContent=msg};
$('stuRotate').onchange=async()=>{const want=+$('stuRotate').value,t0=v.currentTime,a=tin(),b=tout();
 const j=await withProgress('stuSrcProg',t=>post('/rotate',t,JSON.stringify({id:S.src,rotate:want})));
 if(j.error){errTo($('stuSrcMsg'),j.error);$('stuRotate').value=rot;return}
 rot=want;v.addEventListener('loadedmetadata',()=>{setTimes(a,b);v.currentTime=t0},{once:true});v.src=API+'/media/'+S.src+'.mp4?r='+rot+'&'+Date.now()};
/* sources */
function opened(j){if(j.error){errTo($('stuSrcMsg'),j.error);return}
 S.src=j.id;rot=0;$('stuRotate').value='0';if(o.onOpened)o.onOpened(j);v.src=API+'/media/'+j.id+'.mp4';$('stuSrcMsg').textContent='Drag the box to crop, drag its corner to resize. The clip loops between In and Out.'}
$('stuFile').onchange=async()=>{const f=$('stuFile').files[0];if(!f)return;$('stuSrcMsg').textContent='Reading '+f.name+'…';opened(await withProgress('stuSrcProg',t=>post('/open-file',t,f,{'X-Filename':encodeURIComponent(f.name)})))};
$('stuOpenUrl').onclick=async()=>{const u=$('stuUrl').value.trim();if(!u)return;$('stuSrcMsg').textContent='Downloading the first minute of the video…';opened(await withProgress('stuSrcProg',t=>post('/open-url',t,JSON.stringify({url:u}))))};
/* convert + preview */
$('stuConvert').onclick=async()=>{$('stuConvert').disabled=true;$('stuConvMsg').textContent='Converting…';
 const body={id:S.src,rotate:rot,shape:$('stuShape').value,fit:'fill',fps:+$('stuFps').value,start:tin(),duration:tout()-tin(),crop,dither:$('stuDither').checked,threshold:+$('stuThr').value,gain:+$('stuGain').value};
 const j=await withProgress('stuConvProg',t=>post('/convert',t,JSON.stringify(body)));$('stuConvert').disabled=false;
 if(j.error){errTo($('stuConvMsg'),j.error);return}
 Object.assign(S,{work:j.work,bytes:j.bytes,format:j.format,frames:j.frames});
 $('stuConvMsg').textContent=`Converted: ${j.frames} frames (${j.distinct} different), ${j.bytes.toLocaleString()} bytes (${Math.ceil(j.bytes/1024)} KB, format ${j.format}).`;
 S.anim=await decode(Uint8Array.from(atob(j.anm),c=>c.charCodeAt(0)).buffer);animT0=performance.now();$('stuPv').height=S.anim.h;$('stuPv').style.width=S.anim.h>S.anim.w?'128px':'192px';
 if(o.onConverted)o.onConverted(j)};
async function decode(b){if(new Uint8Array(b)[3]===3)return decodeV3(b);const dv=new DataView(b),w=dv.getUint16(4,true),h=dv.getUint16(6,true),n=dv.getUint16(8,true),delays=[],lens=[];for(let i=0;i<n;i++){delays.push(Math.max(20,dv.getUint16(12+i*6,true)));lens.push(dv.getUint32(14+i*6,true))}
 const u=new Uint8Array(b),frames=[],ver=u[3];let p0=12+6*n;for(let i=0;i<n;i++){const end=p0+lens[i];let px;
  if(ver===2)px=await inflateRaw(u.subarray(p0,end),w*h);else{px=new Uint8Array(w*h);let p=0;while(p0<end&&p<px.length){px.fill(u[p0+1],p,Math.min(px.length,p+u[p0]));p+=u[p0];p0+=2}}
  p0=end;frames.push(px)}
 return{w,h,frames,delays,total:delays.reduce((a,d)=>a+d,0)}}
const hex=s=>[1,3,5].map(i=>{const c=parseInt(s.substr(i,2),16);return LV[Math.round(c/85)]});   /* the sign has 4 levels per channel */
const ctx=$('stuPv').getContext('2d');
function paint(){requestAnimationFrame(paint);const anim=S.anim;if(!anim)return;let t=(performance.now()-animT0)%anim.total,fi=0;for(;fi<anim.delays.length;fi++){if(t<anim.delays[fi])break;t-=anim.delays[fi]}
 const px=anim.frames[Math.min(fi,anim.frames.length-1)],img=ctx.createImageData(anim.w,anim.h),d=img.data,mode=+$('stuMode').value,fg=hex($('stuFg').value),bg=hex($('stuBg').value),circ=anim.h===anim.w;
 for(let y=0;y<anim.h;y++)for(let x=0;x<anim.w;x++){const k=(y*anim.w+x)*4;d[k+3]=255;if(circ){const dx=2*x-127,dy=2*y-127;if(dx*dx+dy*dy>16384)continue}
  const p=px[y*anim.w+x];const c=mode?((((p&64)!==0)!==(mode===2))?fg:bg):[LV[(p>>4)&3],LV[(p>>2)&3],LV[p&3]];d[k]=c[0];d[k+1]=c[1];d[k+2]=c[2]}
 ctx.putImageData(img,0,0)}paint();
/* optional packages: say what is missing and how to get it */
fetch(API+'/deps').then(r=>r.json()).then(d=>{S.deps=d;const m=$('stuDeps');
 if(!d.ffmpeg){['stuFile','stuUrl','stuOpenUrl','stuConvert'].forEach(id=>$(id).disabled=true);m.hidden=false;m.textContent=`Making animations needs the Python package${d.missing.length>1?'s':''} ${d.missing.join(' and ')} (yt-dlp only for links). Install, then restart the editor or studio: ${d.install}`}
 else if(!d.ytdlp){['stuUrl','stuOpenUrl'].forEach(id=>$(id).disabled=true);m.hidden=false;m.textContent=`GIFs and videos work; links need the Python package yt-dlp. Install it, then restart: ${d.install}`}
 if(o.onDeps)o.onDeps(d)}).catch(()=>{});
return {state:S,withProgress}};
/* Storage bars, shared by the hosts: segments [{bytes, cls, col, title}] across total; rows [{col, name, size, sub, cls}]. */
AnimStudio.KB=b=>b>=1048576?(b/1048576).toFixed(2)+' MB':Math.ceil(b/1024)+' KB';
AnimStudio.pct=(b,t)=>(100*b/t).toFixed(1)+'%';
AnimStudio.hue=i=>`hsl(${(i*137.5+200)%360} 42% 55%)`;
AnimStudio.bar=function(bar,total,segs){bar.innerHTML='';if(!total)return;segs.forEach(s=>{if(!(s.bytes>0))return;const e=document.createElement('i');e.style.width=AnimStudio.pct(s.bytes,total);if(s.cls)e.className=s.cls;if(s.col)e.style.background=s.col;e.title=s.title||'';bar.appendChild(e)})};
AnimStudio.rows=function(list,rows){list.innerHTML='';rows.forEach(r=>{const li=document.createElement('li');if(r.cls)li.className=r.cls;li.innerHTML='<b></b><span class="nm"></span><span class="sz"></span><em></em>';
 li.querySelector('b').style.background=r.col||'transparent';li.querySelector('.nm').textContent=r.name;li.querySelector('.sz').textContent=r.size||'';li.querySelector('em').textContent=r.sub||'';list.appendChild(li)})};
</script>"""


def fragment(api="/api", out_html=""):
    """The studio's page part: CSS, the sections, and the AnimStudio() script.
    out_html goes into #stuOut under the preview."""
    return FRAGMENT_CSS + FRAGMENT_HTML.replace('<div id="stuOut"></div>', f'<div id="stuOut">{out_html}</div>') + FRAGMENT_JS


# ======================================================================= standalone page
SHELL_HEAD = r"""<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Animation Studio</title><style>
:root{--bg:#14171b;--panel:#1d2127;--rule:#2e343c;--ink:#e8e6e1;--muted:#9aa0a8;--acc:#f2a33a}
body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.45 system-ui,sans-serif}
h1{font-size:20px;margin:16px}.stu{padding:0 16px 24px}
</style></head><body><h1>StopLight animation studio</h1>"""

UPLOAD_HTML = r"""<section class="stu-sec"><h3>5 &middot; Upload</h3>
  <div class="row"><label>Name on the sign <input type="text" id="name" maxlength="24" placeholder="a-z 0-9 - _"></label></div>
  <div class="row"><select id="sign"><option value="http://192.168.4.1">over WiFi (192.168.4.1)</option><option value="http://localhost:8095">into the offline editor (tools/sign_editor.py)</option></select><button id="rescan" title="Look for the sign on USB">&#8635;</button><button class="pri" id="upload" disabled>Upload to sign</button></div><div class="msg" id="upmsg"></div><div class="prog" id="upprog" hidden><span></span><div class="bar"><i></i></div></div></section>
 <section class="stu-sec"><h3>On the sign</h3>
  <div class="msg" id="stmsg">Not read yet.</div><div class="stbar" id="stbar" title="storage on the sign"></div><div class="msg" id="stfit"></div>
  <ul class="stlist" id="stlist"></ul><div class="row"><button id="strefresh">Refresh</button></div></section>"""

UPLOAD_JS = r"""<script>
const $=id=>document.getElementById(id),A=AnimStudio,KB=A.KB,pct=A.pct;
const ST=AnimStudio({api:'/api',onOpened:j=>{if(!$('name').value)$('name').value=j.name.toLowerCase().replace(/[^a-z0-9_-]+/g,'-').slice(0,24)},
 onConverted:j=>{sentAs='';storage();$('upload').disabled=false}}),S=ST.state;
let signPicked=false;
async function ports(){const j=await fetch('/api/ports').then(r=>r.json()).catch(()=>({ports:[]}));const sel=$('sign'),keep=signPicked?sel.value:'';sel.innerHTML='<option value="http://192.168.4.1">over WiFi (192.168.4.1)</option><option value="http://localhost:8095">into the offline editor (tools/sign_editor.py)</option>';
 (j.ports||[]).forEach(p=>{const o=document.createElement('option');o.value=p.port;o.textContent=`over USB cable: ${p.port} (${p.kind} socket)`;sel.appendChild(o)});
 if([...sel.options].some(o=>o.value===keep))sel.value=keep;else if((j.ports||[]).length)sel.value=j.ports[0].port}
$('rescan').onclick=async()=>{await ports();signRefresh()};$('sign').onchange=()=>{signPicked=true;signRefresh()};ports().then(signRefresh);   /* a cable wins until the user picks a target */
/* storage on the sign: every animation's share, the free space, and where the converted clip would go */
let SIGN=null,sentAs='';
const cleanName=()=>$('name').value.trim().toLowerCase().replace(/[^a-z0-9_-]+/g,'-').replace(/^-+|-+$/g,'').slice(0,24);
async function signRefresh(){$('stmsg').textContent='Reading the sign…';$('strefresh').disabled=true;
 SIGN=await fetch('/api/sign-anims?sign='+encodeURIComponent($('sign').value)).then(r=>r.json()).catch(()=>({error:'no answer from the studio'}));$('strefresh').disabled=false;storage()}
function storage(){const list=$('stlist'),bar=$('stbar'),msg=$('stmsg'),fit=$('stfit');list.innerHTML='';bar.innerHTML='';fit.textContent='';fit.className='msg';
 if(!SIGN)return;if(SIGN.error){msg.innerHTML='<span class="err"></span>';msg.firstChild.textContent=SIGN.error;return}
 const convBytes=S.bytes,A_=(SIGN.anims||[]).slice().sort((a,b)=>b.bytes-a.bytes),T=SIGN.total||0,F=SIGN.free||0,used=A_.reduce((n,a)=>n+a.bytes,0),name=cleanName(),sent=A_.find(a=>a.id===sentAs&&a.bytes===convBytes&&sentAs===name),nb=S.work&&!sent?convBytes:0,old=nb?A_.find(a=>a.id===name):null;
 msg.textContent=T?`${KB(T)} of storage: ${A_.length} animation${A_.length===1?'':'s'} use ${KB(used)} (${pct(used,T)}), ${KB(F)} free (${pct(F,T)}).`:`${A_.length} animations, ${KB(used)}. Update the sign's firmware to see its free space over the cable.`;
 const segs=A_.map((a,i)=>({bytes:a.bytes,cls:old&&a.id===old.id?'gone':'',col:A.hue(i),title:`${a.id}: ${KB(a.bytes)} (${pct(a.bytes,T)})`}));
 const fs=T-F-used;segs.push({bytes:fs,cls:'fs',title:`file system: ${KB(fs)} (${pct(fs,T)})`});
 if(nb)segs.push({bytes:Math.min(nb,F+(old?old.bytes:0)),cls:'new',title:`this clip: ${KB(nb)} (${pct(nb,T)})`});A.bar(bar,T,segs);
 const rows=[];if(nb)rows.push({col:'repeating-linear-gradient(45deg,#f2a33a 0 3px,#9c5f1c 3px 6px)',name:`${name||'(no name yet)'} · this clip`,size:`${KB(nb)}${T?' · '+pct(nb,T):''}`,sub:old?`replaces the one with this name (${KB(old.bytes)})`:'not on the sign yet',cls:'new'});
 A_.forEach((a,i)=>rows.push({col:A.hue(i),name:a.id,size:`${KB(a.bytes)}${T?' · '+pct(a.bytes,T):''}`,sub:`${a.full?'whole sign':'one light'} · ${(a.ms/1000).toFixed(1)} s · ${a.frames} frames${old&&a.id===old.id?' · will be replaced':''}`}));
 if(!rows.length)rows.push({name:'No animations yet.'});A.rows(list,rows);
 if(nb&&T){const left=F+(old?old.bytes:0)-nb;
  if(left>=8192)fit.textContent=`Uploading this clip${name?' as "'+name+'"':''} leaves ${KB(left)} free (${pct(left,T)}).`;
  else{fit.className='msg err';fit.textContent=`This clip does not fit: ${KB(8192-left)} short. Delete or shorten an animation first.`}}}
$('strefresh').onclick=signRefresh;$('name').addEventListener('input',storage);
$('upload').onclick=async()=>{const name=cleanName();
 if(!name){$('upmsg').textContent='Give the animation a name first.';$('name').focus();return}$('name').value=name;
 $('upmsg').textContent='Checking the sign…';
 const have=await fetch('/api/sign-anims?sign='+encodeURIComponent($('sign').value)).then(r=>r.json()).catch(()=>({error:'no answer'}));
 if(have.error){$('upmsg').innerHTML='<span class="err"></span>';$('upmsg').firstChild.textContent=have.error;return}
 SIGN=have;storage();
 const old=(have.anims||[]).find(a=>a.id===name);
 if(have.total&&S.bytes+8192>have.free+(old?old.bytes:0)){$('upmsg').innerHTML='<span class="err"></span>';$('upmsg').firstChild.textContent=`Not uploaded: the clip needs ${KB(S.bytes)} and the sign has ${KB(have.free)} free. Delete or shorten an animation first.`;return}
 if(old&&!confirm(`The sign already has an animation called "${name}" (${Math.ceil(old.bytes/1024)} KB, ${(old.ms/1000).toFixed(1)} s). Replace it? Sets that use it will show the new one.\n\nCancel to keep it, then change the name and upload again.`)){$('upmsg').textContent='Not uploaded: change the name to keep both.';$('name').focus();return}$('upload').disabled=true;$('upmsg').textContent='Uploading…';
 const j=await ST.withProgress('upprog',t=>fetch('/api/upload',{method:'POST',headers:{'X-Token':t},body:JSON.stringify({name,work:S.work,sign:$('sign').value})}).then(r=>r.json()).catch(e=>({error:String(e)})));$('upload').disabled=false;
 if(j.error){$('upmsg').innerHTML='<span class="err"></span>';$('upmsg').firstChild.textContent=j.error;return}
 $('upmsg').textContent=`On the sign as "${name}"${j.port?' (via '+j.port+')':''}. ${Math.floor((j.free||0)/1024)} KB left there. Pick it in a light's graphic list (reload the sign's page).`;sentAs=name;signRefresh()};
</script>"""


def page():
    with open(os.path.join(HERE, "anim_v3.js"), encoding="utf-8") as f:   # the browser decoder, as in src/WebPage.h
        v3 = f.read()
    inflate = ("async function inflateRaw(bytes,size){const out=new Uint8Array(size);try{const r=new Blob([bytes]).stream().pipeThrough(new DecompressionStream('deflate-raw')).getReader();"
               "let p=0;for(;;){const{done,value}=await r.read();if(done)break;out.set(value.subarray(0,Math.max(0,size-p)),p);p+=value.length}}catch(e){}return out}\n")
    return (SHELL_HEAD + fragment("/api", UPLOAD_HTML) + "<script>\n" + v3 + inflate + "</script>" + UPLOAD_JS
            + "</body></html>")


class H(BaseHTTPRequestHandler):
    studio = None

    def log_message(self, *a):
        pass

    def _json(self, obj, code=200):
        b = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def do_GET(self):
        if self.path == "/":
            b = page().encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(b)))
            self.end_headers()
            self.wfile.write(b)
            return
        if self.studio.handle_get(self, self.path):
            return
        if self.path.startswith("/api/sign-anims"):   # what is on the sign now (WiFi or cable)
            t = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query).get("sign", ["http://192.168.4.1"])[0]
            return self._json(ac.sign_list(t))
        if self.path == "/api/ports":
            try:
                return self._json({"ports": [{"port": p, "kind": k} for p, k in ac.sign_ports()]})
            except ImportError:
                return self._json({"ports": []})
        self.send_error(404)

    def do_POST(self):
        if self.studio.handle_post(self, self.path):
            return
        if self.path == "/api/upload":
            cb = self.studio.reporter(self.headers.get("X-Token", ""))
            try:
                o = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))))
                name = re.sub(r"[^a-z0-9_-]+", "-", (o.get("name") or "").lower()).strip("-")[:24]
                if not name:
                    return self._json({"error": "give the animation a name"}, 400)
                p = self.studio.work_path(o.get("work"))
                if not p or not os.path.exists(p):
                    return self._json({"error": "convert first"}, 400)
                data = open(p, "rb").read()
                res = ac.upload_to(o.get("sign") or "http://192.168.4.1", name, data, cb)
                if "error" not in res:
                    with open(os.path.join(GFX, name + ".anm"), "wb") as f:   # a local copy under the name it has on the sign
                        f.write(data)
                    res["name"] = name
                return self._json(res, 400 if "error" in res else 200)
            except Exception as e:
                return self._json({"error": str(e)}, 500)
        self.send_error(404)


def main():
    global PORT
    if "--port" in sys.argv:
        PORT = int(sys.argv[sys.argv.index("--port") + 1])
    os.makedirs(SRC, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)
    H.studio = Studio(SRC, WORK, "/api")
    srv = ThreadingHTTPServer(("127.0.0.1", PORT), H)
    print(f"Animation studio on http://localhost:{PORT}/  (Ctrl+C to stop; the offline editor tools/sign_editor.py has the same studio built in)")
    if "--no-browser" not in sys.argv:
        threading.Timer(0.8, lambda: webbrowser.open(f"http://localhost:{PORT}/")).start()
    srv.serve_forever()


if __name__ == "__main__":
    main()
