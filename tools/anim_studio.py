#!/usr/bin/env python3
"""StopLight animation studio: a local web app that turns a GIF, a video file
or a YouTube link into a sign animation (.anm), with a crop box, in/out
times, a preview of what the LEDs will show, and upload to the sign.

    python.exe tools/anim_studio.py        # then open http://localhost:8090/

Run it with Windows Python: it uses ffmpeg (imageio-ffmpeg), yt-dlp, numpy
and Pillow from there, and only Windows can reach the sign's WiFi (USB
dongle joined to 'StopLight'). Sources are kept in tools/gfx/sources/,
results in tools/gfx/<name>.anm. The conversion itself is anim_convert.py.
"""
import urllib.parse
import base64, json, mimetypes, os, re, subprocess, sys, tempfile, threading, urllib.request, uuid, webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import anim_convert as ac

HERE = os.path.dirname(os.path.abspath(__file__))
GFX = os.path.join(HERE, "gfx")
SRC = os.path.join(GFX, "sources")
WORK = os.path.join(GFX, "work")       # converted, not yet named / uploaded
os.makedirs(SRC, exist_ok=True)
os.makedirs(WORK, exist_ok=True)
PORT = 8090


PROGRESS = {}   # token -> {phase, done, total, unit}; the page polls /api/progress?t=token


def reporter(token):
    def cb(phase, done, total, unit=""):
        if token:
            PROGRESS[token] = {"phase": phase, "done": done, "total": total, "unit": unit}
    return cb


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


def source_path(sid):
    for fn in os.listdir(SRC):
        if fn.startswith(sid + ".src"):
            return os.path.join(SRC, fn)
    return None


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _json(self, obj, code=200):
        b = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def _body(self):
        return self.rfile.read(int(self.headers.get("Content-Length", 0)))

    def do_GET(self):
        if self.path == "/":
            b = PAGE.encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(b)))
            self.end_headers()
            self.wfile.write(b)
            return
        if self.path.startswith("/api/progress"):
            t = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query).get("t", [""])[0]
            return self._json(PROGRESS.get(t, {}))
        if self.path.startswith("/api/sign-anims"):   # what is on the sign now (WiFi or cable)
            t = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query).get("sign", ["http://192.168.4.1"])[0]
            return self._json(ac.sign_list(t))
        if self.path == "/api/ports":
            return self._json({"ports": [{"port": p, "kind": k} for p, k in ac.sign_ports()]})
        m = re.match(r"^/media/([0-9a-f]+)\.mp4(\?.*)?$", self.path)
        if m:
            p = os.path.join(SRC, m.group(1) + ".mp4")
            if not os.path.exists(p):
                return self.send_error(404)
            size = os.path.getsize(p)
            start, end = 0, size - 1
            rng = re.match(r"bytes=(\d*)-(\d*)", self.headers.get("Range", ""))
            if rng:
                if rng.group(1):
                    start = int(rng.group(1))
                if rng.group(2):
                    end = min(int(rng.group(2)), size - 1)
            with open(p, "rb") as f:
                f.seek(start)
                data = f.read(end - start + 1)
            self.send_response(206 if rng else 200)
            self.send_header("Content-Type", "video/mp4")
            self.send_header("Accept-Ranges", "bytes")
            self.send_header("Content-Length", str(len(data)))
            if rng:
                self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
            self.end_headers()
            self.wfile.write(data)
            return
        self.send_error(404)

    def do_POST(self):
        cb = reporter(self.headers.get("X-Token", ""))
        try:
            if self.path == "/api/open-file":
                name = urllib.request.unquote(self.headers.get("X-Filename", "clip"))
                sid = uuid.uuid4().hex[:10]
                ext = os.path.splitext(name)[1].lower() or ".bin"
                src = os.path.join(SRC, sid + ".src" + ext)
                with open(src, "wb") as f:
                    f.write(self._body())
                make_proxy(src, os.path.join(SRC, sid + ".mp4"), cb)
                return self._json({"id": sid, "name": os.path.splitext(name)[0]})
            if self.path == "/api/open-url":
                url = json.loads(self._body())["url"].strip()
                sid = uuid.uuid4().hex[:10]
                with tempfile.TemporaryDirectory() as tmp:
                    got = ac.download(url, tmp, 60, cb)
                    src = os.path.join(SRC, sid + ".src" + os.path.splitext(got)[1])
                    os.replace(got, src)
                make_proxy(src, os.path.join(SRC, sid + ".mp4"), cb)
                return self._json({"id": sid, "name": "clip"})
            if self.path == "/api/rotate":   # remake the preview turned, so the crop box works on what you see
                o = json.loads(self._body())
                src = source_path(o["id"])
                if not src:
                    return self._json({"error": "source gone, open it again"}, 400)
                make_proxy(src, os.path.join(SRC, o["id"] + ".mp4"), cb, int(o.get("rotate", 0)))
                return self._json({"ok": True})
            if self.path == "/api/convert":
                o = json.loads(self._body())
                src = source_path(o["id"])
                if not src:
                    return self._json({"error": "source gone, open it again"}, 400)
                data, n, distinct = ac.convert(src, o.get("shape", "circle"), float(o.get("fps", 12)),
                                               float(o.get("start", 0)), float(o.get("duration", 0)), o.get("fit", "fill"),
                                               tuple(o["crop"]) if o.get("crop") else None, bool(o.get("dither")),
                                               float(o.get("threshold", 0.5)), float(o.get("gain", 1.0)),
                                               int(o.get("maxFrames", 300)), cb, True, int(o.get("rotate", 0)))
                with open(os.path.join(WORK, o["id"] + ".anm"), "wb") as f:   # named only when uploaded
                    f.write(data)
                return self._json({"work": o["id"], "frames": n, "distinct": distinct, "bytes": len(data),
                                   "anm": base64.b64encode(data).decode()})
            if self.path == "/api/upload":
                o = json.loads(self._body())
                name = re.sub(r"[^a-z0-9_-]+", "-", (o.get("name") or "").lower()).strip("-")[:24]
                if not name:
                    return self._json({"error": "give the animation a name"}, 400)
                p = os.path.join(WORK, re.sub(r"[^0-9a-f]", "", o.get("work", "")) + ".anm")
                if not os.path.exists(p):
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


PAGE = r"""<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Animation Studio</title><style>
:root{--bg:#14171b;--panel:#1d2127;--rule:#2e343c;--ink:#e8e6e1;--muted:#9aa0a8;--acc:#f2a33a}
body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.45 system-ui,sans-serif}
h1{font-size:20px;margin:16px}h2{font-size:15px;margin:0 0 10px;color:var(--muted);font-weight:600}
.wrap{display:grid;grid-template-columns:minmax(0,1fr) 300px;gap:16px;padding:0 16px 24px}
@media(max-width:900px){.wrap{grid-template-columns:1fr}}
.card{background:var(--panel);border:1px solid var(--rule);border-radius:8px;padding:14px;margin-bottom:16px}
.row{display:flex;flex-wrap:wrap;gap:8px 14px;align-items:center;margin:8px 0}
select{max-width:100%}
input,select,button{font:inherit;color:var(--ink);background:#111418;border:1px solid var(--rule);border-radius:6px;padding:5px 8px}
input[type=number]{width:5em}input[type=text]{min-width:0;flex:1}button{cursor:pointer}button.pri{background:var(--acc);color:#14171b;border-color:var(--acc);font-weight:600}
button:disabled{opacity:.5;cursor:default}
.stage{position:relative;display:inline-block;max-width:100%;line-height:0}video{max-width:100%;max-height:60vh;display:block;background:#000}
#crop{position:absolute;border:2px solid var(--acc);box-shadow:0 0 0 9999px rgba(0,0,0,.55);cursor:move;box-sizing:border-box}
#crop i{position:absolute;right:-7px;bottom:-7px;width:14px;height:14px;background:var(--acc);border-radius:50%;cursor:nwse-resize}
#crop.circle::after{content:"";position:absolute;inset:0;border-radius:50%;border:1px dashed rgba(242,163,58,.8)}
.stage-wrap{overflow:hidden}
.msg{color:var(--muted);font-size:13px;min-height:1.2em}.err{color:#ff8a7a}
canvas{background:#000;image-rendering:pixelated;display:block;margin:0 auto}
.prog{margin:6px 0 0;font-size:13px;color:var(--muted)}.prog[hidden]{display:none}.prog .bar{height:8px;background:#0c0e11;border:1px solid var(--rule);border-radius:4px;overflow:hidden;margin-top:3px}
.prog .bar i{display:block;height:100%;width:0;background:var(--acc);transition:width .25s}.prog .bar.ind i{width:30%;animation:ind 1.1s linear infinite}@keyframes ind{from{margin-left:-30%}to{margin-left:100%}}
.stbar{display:flex;height:14px;background:#0c0e11;border:1px solid var(--rule);border-radius:4px;overflow:hidden;margin:8px 0}
.stbar i{flex:none;height:100%;box-shadow:inset -1px 0 #14171b}.stbar i.fs{background:#3a4048}.stbar i.gone{opacity:.3}
.stbar i.new{background:repeating-linear-gradient(45deg,var(--acc) 0 4px,#9c5f1c 4px 8px)}
#stlist{list-style:none;margin:6px 0 0;padding:0;font-size:13px}
#stlist li{display:grid;grid-template-columns:10px minmax(0,1fr) auto;gap:0 8px;align-items:center;padding:4px 0;border-top:1px solid var(--rule)}
#stlist b{width:10px;height:10px;border-radius:2px}#stlist .nm{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
#stlist .sz{font-variant-numeric:tabular-nums}#stlist em{grid-column:2/4;color:var(--muted);font-style:normal;font-size:12px}
#stlist li.new .nm{color:var(--acc)}
.range{position:relative;height:28px}.range input{position:absolute;inset:0;width:100%;pointer-events:none;background:none;border:0;padding:0}
.range input::-webkit-slider-thumb{pointer-events:auto}.range input::-moz-range-thumb{pointer-events:auto}
</style></head><body><h1>StopLight animation studio</h1>
<div class="wrap"><div>
 <div class="card"><h2>1 · Source</h2>
  <div class="row"><input type="file" id="file" accept="image/gif,video/*,image/webp"><span class="msg">or</span><input type="text" id="url" placeholder="YouTube or other video link"><button id="openUrl">Open link</button></div>
  <div class="msg" id="srcmsg">Pick a GIF or a video, or paste a link.</div><div class="prog" id="srcprog" hidden><span></span><div class="bar"><i></i></div></div></div>
 <div class="card" id="editcard" hidden><h2>2 · Crop and trim</h2>
  <div class="stage-wrap"><div class="stage"><video id="v" muted loop playsinline controls></video><div id="crop"><i></i></div></div></div>
  <div class="row"><label><input type="checkbox" id="lock" checked> keep the shape's proportions</label><label>Rotate <select id="rotate"><option value="0">none</option><option value="90">90&deg; right</option><option value="180">180&deg;</option><option value="270">90&deg; left</option></select></label><button id="cropAll">Whole picture</button><button id="cropFit">Largest crop</button><span class="msg" id="cropmsg"></span></div>
  <div class="range"><input type="range" id="rin" min="0" max="1000" value="0"><input type="range" id="rout" min="0" max="1000" value="1000"></div>
  <div class="row"><span>In <input type="number" id="tin" step="0.1" min="0"> s <button id="setIn">= now</button></span><span>Out <input type="number" id="tout" step="0.1" min="0"> s <button id="setOut">= now</button></span><b id="len"></b></div>
 </div></div>
 <div>
 <div class="card"><h2>3 · Output</h2>
  <div class="row"><label>Shape <select id="shape"><option value="circle">one light (128×128)</option><option value="full">whole sign (128×384)</option></select></label></div>
  <div class="row"><label>Frames/s <input type="number" id="fps" value="12" min="1" max="30" step="1"></label><label><input type="checkbox" id="dither"> dither</label></div>
  <div class="row"><label>Brightness × <input type="number" id="gain" value="1" min="0.2" max="4" step="0.1"></label><label>Silhouette at <input type="number" id="thr" value="0.5" min="0.05" max="0.95" step="0.05"></label></div>
  <div class="row"><button class="pri" id="convert" disabled>Convert</button></div><div class="msg" id="convmsg"></div><div class="prog" id="convprog" hidden><span></span><div class="bar"><i></i></div></div></div>
 <div class="card"><h2>4 · Preview and upload</h2>
  <div class="row"><select id="mode"><option value="0">original colours</option><option value="1">silhouette</option><option value="2">silhouette inverted</option></select><input type="color" id="fg" value="#ffffff" title="silhouette colour"><input type="color" id="bg" value="#ff0000" title="background colour"></div>
  <canvas id="pv" width="128" height="128" style="width:192px"></canvas>
  <div class="row"><label>Name on the sign <input type="text" id="name" maxlength="24" placeholder="a-z 0-9 - _"></label></div>
  <div class="row"><select id="sign"><option value="http://192.168.4.1">over WiFi (192.168.4.1)</option></select><button id="rescan" title="Look for the sign on USB">&#8635;</button><button class="pri" id="upload" disabled>Upload to sign</button></div><div class="msg" id="upmsg"></div><div class="prog" id="upprog" hidden><span></span><div class="bar"><i></i></div></div></div>
 <div class="card"><h2>On the sign</h2>
  <div class="msg" id="stmsg">Not read yet.</div><div class="stbar" id="stbar" title="storage on the sign"></div><div class="msg" id="stfit"></div>
  <ul id="stlist"></ul><div class="row"><button id="strefresh">Refresh</button></div></div>
 </div></div>
<script>
const $=id=>document.getElementById(id),v=$('v'),cropEl=$('crop');
let src=null,crop=[0,0,1,1],anim=null,animT0=0,rot=0,work=null;
const LV=[0,140,195,255];
const aspect=()=>$('shape').value==='full'?128/384:1;   /* target width / height in pixels */
function vw(){return v.videoWidth||1}function vh(){return v.videoHeight||1}
function fitCrop(){const a=aspect(),srcA=vw()/vh();let w=1,h=1;if(srcA>a){w=a/srcA}else{h=srcA/a}crop=[(1-w)/2,(1-h)/2,w,h];drawCrop()}
function drawCrop(){const W=v.clientWidth,H=v.clientHeight;cropEl.style.left=crop[0]*W+'px';cropEl.style.top=crop[1]*H+'px';cropEl.style.width=crop[2]*W+'px';cropEl.style.height=crop[3]*H+'px';
 cropEl.classList.toggle('circle',$('shape').value==='circle');$('cropmsg').textContent=`${Math.round(crop[2]*vw())}×${Math.round(crop[3]*vh())} px of ${vw()}×${vh()}`}
function clamp(){crop[2]=Math.min(Math.max(crop[2],0.02),1);crop[3]=Math.min(Math.max(crop[3],0.02),1);crop[0]=Math.min(Math.max(crop[0],0),1-crop[2]);crop[1]=Math.min(Math.max(crop[1],0),1-crop[3])}
let drag=null;
cropEl.addEventListener('pointerdown',e=>{e.preventDefault();cropEl.setPointerCapture(e.pointerId);drag={resize:e.target.tagName==='I',x:e.clientX,y:e.clientY,c:crop.slice()}});
cropEl.addEventListener('pointermove',e=>{if(!drag)return;const dx=(e.clientX-drag.x)/v.clientWidth,dy=(e.clientY-drag.y)/v.clientHeight;
 if(drag.resize){let w=drag.c[2]+dx,h=drag.c[3]+dy;if($('lock').checked){const a=aspect()*vh()/vw();h=w/a;if(drag.c[1]+h>1){h=1-drag.c[1];w=h*a}if(drag.c[0]+w>1){w=1-drag.c[0];h=w/a}}crop=[drag.c[0],drag.c[1],w,h]}
 else crop=[drag.c[0]+dx,drag.c[1]+dy,drag.c[2],drag.c[3]];clamp();drawCrop()});
cropEl.addEventListener('pointerup',()=>drag=null);
$('cropAll').onclick=()=>{crop=[0,0,1,1];$('lock').checked=false;drawCrop()};$('cropFit').onclick=()=>{$('lock').checked=true;fitCrop()};
$('shape').onchange=()=>{if($('lock').checked)fitCrop();else drawCrop()};
window.addEventListener('resize',drawCrop);
/* in / out */
const dur=()=>v.duration&&isFinite(v.duration)?v.duration:0;
function setTimes(a,b){const d=dur();a=Math.max(0,Math.min(a,d));b=Math.max(a+0.05,Math.min(b,d));$('tin').value=a.toFixed(2);$('tout').value=b.toFixed(2);$('rin').value=d?a/d*1000:0;$('rout').value=d?b/d*1000:1000;
 const L=b-a,frames=Math.ceil(L*(+$('fps').value||12));$('len').textContent=`${L.toFixed(2)} s · ${frames} frames`}
const tin=()=>+$('tin').value||0,tout=()=>+$('tout').value||dur();
$('rin').oninput=()=>{setTimes($('rin').value/1000*dur(),tout());v.currentTime=tin()};$('rout').oninput=()=>{setTimes(tin(),$('rout').value/1000*dur());v.currentTime=Math.max(tin(),tout()-0.3)};
$('tin').onchange=()=>setTimes(tin(),tout());$('tout').onchange=()=>setTimes(tin(),tout());$('fps').onchange=()=>setTimes(tin(),tout());
$('setIn').onclick=()=>setTimes(v.currentTime,tout());$('setOut').onclick=()=>setTimes(tin(),v.currentTime);
v.addEventListener('timeupdate',()=>{if(v.currentTime>=tout()||v.currentTime<tin()-0.05)v.currentTime=tin()});
v.addEventListener('loadedmetadata',()=>{$('editcard').hidden=false;setTimeout(()=>{fitCrop();setTimes(0,Math.min(dur(),6))},50);v.play().catch(()=>{});$('convert').disabled=false});
/* progress: the server records it per token while a request runs */
const PH={download:'Downloading',prepare:'Preparing the preview',decode:'Reading frames',colours:'Converting colours',upload:'Uploading'};
function fmt(v,u){return u==='bytes'?(v/1048576).toFixed(v<1048576?2:1)+' MB':u==='s'?v.toFixed(1)+' s':v+(u?' '+u:'')}
async function withProgress(box,req){const token=Math.random().toString(36).slice(2),el=$(box),bar=el.querySelector('.bar'),fill=bar.querySelector('i'),lab=el.querySelector('span');
 el.hidden=false;bar.classList.add('ind');fill.style.width='';lab.textContent='Starting…';let done=false;
 const poll=async()=>{while(!done){const p=await fetch('/api/progress?t='+token).then(r=>r.json()).catch(()=>({}));
  if(p.phase&&!done){const pct=p.total?Math.min(100,Math.round(100*p.done/p.total)):null;bar.classList.toggle('ind',pct===null);if(pct!==null)fill.style.width=pct+'%';
   lab.textContent=`${PH[p.phase]||p.phase}: ${fmt(p.done,p.unit)}${p.total?' of '+fmt(p.total,p.unit)+' ('+pct+'%)':''}`}
  await new Promise(r=>setTimeout(r,300))}};poll();
 try{return await req(token)}finally{done=true;el.hidden=true}}
$('rotate').onchange=async()=>{const want=+$('rotate').value,t0=v.currentTime,a=tin(),b=tout();
 const j=await withProgress('srcprog',t=>fetch('/api/rotate',{method:'POST',headers:{'X-Token':t},body:JSON.stringify({id:src,rotate:want})}).then(r=>r.json()).catch(e=>({error:String(e)})));
 if(j.error){$('srcmsg').textContent=j.error;$('rotate').value=rot;return}
 rot=want;v.addEventListener('loadedmetadata',()=>{setTimes(a,b);v.currentTime=t0},{once:true});v.src='/media/'+src+'.mp4?r='+rot+'&'+Date.now()};
/* sources */
async function opened(r){const j=await r.json().catch(()=>({error:'no reply'}));if(j.error){$('srcmsg').innerHTML='<span class="err"></span>';$('srcmsg').firstChild.textContent=j.error;return}
 src=j.id;rot=0;$('rotate').value='0';if(!$('name').value)$('name').value=j.name.toLowerCase().replace(/[^a-z0-9_-]+/g,'-').slice(0,24);v.src='/media/'+j.id+'.mp4';$('srcmsg').textContent='Drag the box to crop, drag its corner to resize. The clip loops between In and Out.'}
$('file').onchange=async()=>{const f=$('file').files[0];if(!f)return;$('srcmsg').textContent='Reading '+f.name+'…';opened(await withProgress('srcprog',t=>fetch('/api/open-file',{method:'POST',headers:{'X-Filename':encodeURIComponent(f.name),'X-Token':t},body:f})))};
$('openUrl').onclick=async()=>{const u=$('url').value.trim();if(!u)return;$('srcmsg').textContent='Downloading the first minute of the video…';opened(await withProgress('srcprog',t=>fetch('/api/open-url',{method:'POST',headers:{'X-Token':t},body:JSON.stringify({url:u})})))};
/* convert + preview */
$('convert').onclick=async()=>{$('convert').disabled=true;$('convmsg').textContent='Converting…';
 const body={id:src,rotate:rot,shape:$('shape').value,fit:'fill',fps:+$('fps').value,start:tin(),duration:tout()-tin(),crop,dither:$('dither').checked,threshold:+$('thr').value,gain:+$('gain').value};
 const j=await withProgress('convprog',t=>fetch('/api/convert',{method:'POST',headers:{'X-Token':t},body:JSON.stringify(body)}).then(r=>r.json()).catch(e=>({error:String(e)})));$('convert').disabled=false;
 if(j.error){$('convmsg').innerHTML='<span class="err"></span>';$('convmsg').firstChild.textContent=j.error;return}
 work=j.work;convBytes=j.bytes;sentAs='';storage();$('convmsg').textContent=`Converted: ${j.frames} frames (${j.distinct} different), ${Math.ceil(j.bytes/1024)} KB. Name it and upload below.`;
 anim=await decode(Uint8Array.from(atob(j.anm),c=>c.charCodeAt(0)).buffer);animT0=performance.now();$('pv').height=anim.h;$('pv').style.width=anim.h>anim.w?'128px':'192px';$('upload').disabled=false};
// Version 3 animation decoder for the browser (mirror of src/AnimV3.cpp and
// tools/anim_v3.py; must stay bit-exact with them).  ES2017, no dependencies.
// decodeV3(arrayBuffer) -> {w, h, frames: [Uint8Array(w*h)...], delays: [ms...], total}
// Pixel bytes: bits 0-5 = 0bRRGGBB, bit 6 = silhouette light.
function decodeV3(buf) {
  var d = new Uint8Array(buf);
  if (d.length < 20 || d[0] !== 83 || d[1] !== 65 || d[2] !== 78 || d[3] !== 3) throw new Error("not a v3 animation");
  var u16 = function (o) { return d[o] | (d[o + 1] << 8); };
  var u32 = function (o) { return (d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (d[o + 3] << 24)) >>> 0; };
  var w = u16(4), h = u16(6), n = u16(8), K = d[12], ctxset = d[13], flags = d[14], nkeys = u16(16), STATES = u16(18);
  var OFF_S = 16, OFF_R = 25, OFF_G = OFF_R + 768, OFF_B = OFF_G + 3072, OFF_L = OFF_B + 3072;
  if (STATES !== OFF_L + 512 || w === 0 || w > 128 || h === 0 || n === 0 || K === 0 || ctxset > 1) throw new Error("bad v3 header");
  var masked = (flags & 2) !== 0;   // only the circle (diameter 132) of each 128x128 tile is coded, the rest is black
  if ((flags & ~3) || (masked && (w !== 128 || h % 128))) throw new Error("bad v3 flags");
  var spanS = new Uint8Array(128), spanE = new Uint8Array(128);
  for (i = 0; i < 128; i++) {
    var dy = 2 * i - 127, r2 = 132 * 132 - dy * dy, xs = 0, xe = 128;
    if (r2 < 0) xs = xe = 0;
    while (xs < xe && (2 * xs - 127) * (2 * xs - 127) > r2) xs++;
    while (xe > xs && (2 * (xe - 1) - 127) * (2 * (xe - 1) - 127) > r2) xe--;
    spanS[i] = xs; spanE[i] = xe;
  }
  var delays = [], lens = [], total = 0, i;
  for (i = 0; i < n; i++) {
    var ms = Math.max(20, u16(20 + 6 * i));
    delays.push(ms); total += ms; lens.push(u32(22 + 6 * i));
  }
  var priorOff = 20 + 6 * n + 4 * nkeys;
  var prior = d.subarray(priorOff, priorOff + STATES);
  var off = u32(20 + 6 * n);   // first keyframe = first frame
  var st = new Uint16Array(STATES);
  var npx = w * h, frames = [], prev = null;
  var tf = new Uint8Array(npx), sf = new Uint8Array(npx);
  // range decoder state (all kept as unsigned 32-bit numbers)
  var pos = 0, end = 0, range = 0, code = 0;
  var byte = function () { return pos < end ? d[pos++] : 0; };
  var bit = function (si) {
    var s = st[si], p = s >> 4, cnt = s & 15, sh = cnt < 5 ? cnt + 1 : 5;
    var bound = ((range >>> 12) * p) >>> 0, b;
    if (code < bound) { range = bound; b = 0; p += (4080 - p) >> sh; }
    else { code = (code - bound) >>> 0; range = (range - bound) >>> 0; b = 1; p -= (p - 16) >> sh; }
    st[si] = (p << 4) | (cnt < 15 ? cnt + 1 : 15);
    while (range < 16777216) { range = (range << 8) >>> 0; code = ((code << 8) | byte()) >>> 0; }
    return b;
  };
  for (var f = 0; f < n; f++) {
    pos = off; end = off + lens[f]; off = end;
    var temporal = (byte() & 1) !== 0;
    if (temporal && (prev === null || f % K === 0)) throw new Error("bad temporal frame " + f);
    range = 0xFFFFFFFF; code = 0;
    for (i = 0; i < 5; i++) code = ((code << 8) | byte()) >>> 0;
    for (i = 0; i < STATES; i++) st[i] = (prior[i] << 8) | 3;
    var cur = new Uint8Array(npx);
    for (var y = 0; y < h; y++) {
      var row = y * w, up1 = row - w, up4 = row - 4 * w, x0 = 0, x1 = w;
      if (masked) {   // hidden pixels: black (cur is zero-filled), tf 0, sf 2, no decisions
        x0 = spanS[y & 127]; x1 = spanE[y & 127];
        tf.fill(0, row, row + w); sf.fill(2, row, row + w);
      }
      for (var x = x0; x < x1; x++) {
        var o = row + x;
        var l1 = x >= 1 ? cur[o - 1] : 0, u1 = y >= 1 ? cur[up1 + x] : 0;
        var l4 = x >= 4 ? cur[o - 4] : 0, u4 = y >= 4 ? cur[up4 + x] : 0;
        var tl = x ? tf[o - 1] : 0, tu = y ? tf[o - w] : 0;
        var sl = x ? sf[o - 1] : 0, su = y ? sf[o - w] : 0;
        var t = 0;
        if (temporal) {
          t = prev[o];
          var th = bit(tl * 8 + tu * 4 + (l1 === t ? 2 : 0) + (u1 === t ? 1 : 0));
          tf[o] = th;
          if (th) { cur[o] = t; sf[o] = 2; continue; }
        } else tf[o] = 0;
        if (l1 === u1 && l1 === l4 && l1 === u4) {
          var shit = bit(OFF_S + sl * 3 + su);
          sf[o] = shit;
          if (shit) { cur[o] = l1; continue; }
        } else sf[o] = 2;
        var n2, n3;
        if (ctxset === 0) { n2 = l4; n3 = u4; }
        else { n2 = (x >= 1 && y >= 1) ? cur[up1 + x - 1] : 0; n3 = (x + 1 < w && y >= 1) ? cur[up1 + x + 1] : 0; }
        var colour = 0, prevCh = 0;
        for (var ch = 0; ch < 3; ch++) {
          var shf = 4 - 2 * ch;
          var c = (((l1 >> shf) & 3) << 6) | (((u1 >> shf) & 3) << 4) | (((n2 >> shf) & 3) << 2) | ((n3 >> shf) & 3);
          var base = ch === 0 ? OFF_R + c * 3 : (ch === 1 ? OFF_G : OFF_B) + ((c << 2) | prevCh) * 3;
          var hi = bit(base), lo = bit(base + 1 + hi);
          prevCh = hi * 2 + lo;
          colour = (colour << 2) | prevCh;
        }
        var light = bit(OFF_L + ((l1 >> 6) & 1) * 256 + ((u1 >> 6) & 1) * 128 + (temporal ? (t >> 6) & 1 : 0) * 64 + colour);
        cur[o] = (light << 6) | colour;
      }
    }
    frames.push(cur);
    prev = cur;
  }
  return { w: w, h: h, frames: frames, delays: delays, total: total };
}
async function inflateRaw(bytes,size){const out=new Uint8Array(size);try{const r=new Blob([bytes]).stream().pipeThrough(new DecompressionStream('deflate-raw')).getReader();let p=0;for(;;){const{done,value}=await r.read();if(done)break;out.set(value.subarray(0,Math.max(0,size-p)),p);p+=value.length}}catch(e){}return out}
async function decode(b){if(new Uint8Array(b)[3]===3)return decodeV3(b);const v=new DataView(b),w=v.getUint16(4,true),h=v.getUint16(6,true),n=v.getUint16(8,true),delays=[],lens=[];for(let i=0;i<n;i++){delays.push(Math.max(20,v.getUint16(12+i*6,true)));lens.push(v.getUint32(14+i*6,true))}
 const u=new Uint8Array(b),frames=[],ver=u[3];let o=12+6*n;for(let i=0;i<n;i++){const end=o+lens[i];let px;
  if(ver===2)px=await inflateRaw(u.subarray(o,end),w*h);else{px=new Uint8Array(w*h);let p=0;while(o<end&&p<px.length){px.fill(u[o+1],p,Math.min(px.length,p+u[o]));p+=u[o];o+=2}}
  o=end;frames.push(px)}
 return{w,h,frames,delays,total:delays.reduce((a,d)=>a+d,0)}}
const hex=s=>[1,3,5].map(i=>{const c=parseInt(s.substr(i,2),16);return LV[Math.round(c/85)]});   /* the sign has 4 levels per channel */
const ctx=$('pv').getContext('2d');
function paint(){requestAnimationFrame(paint);if(!anim)return;let t=(performance.now()-animT0)%anim.total,fi=0;for(;fi<anim.delays.length;fi++){if(t<anim.delays[fi])break;t-=anim.delays[fi]}
 const px=anim.frames[Math.min(fi,anim.frames.length-1)],img=ctx.createImageData(anim.w,anim.h),d=img.data,mode=+$('mode').value,fg=hex($('fg').value),bg=hex($('bg').value),circ=anim.h===anim.w;
 for(let y=0;y<anim.h;y++)for(let x=0;x<anim.w;x++){const k=(y*anim.w+x)*4;d[k+3]=255;if(circ){const dx=2*x-127,dy=2*y-127;if(dx*dx+dy*dy>16384)continue}
  const p=px[y*anim.w+x];const c=mode?((((p&64)!==0)!==(mode===2))?fg:bg):[LV[(p>>4)&3],LV[(p>>2)&3],LV[p&3]];d[k]=c[0];d[k+1]=c[1];d[k+2]=c[2]}
 ctx.putImageData(img,0,0)}paint();
let signPicked=false;
async function ports(){const j=await fetch('/api/ports').then(r=>r.json()).catch(()=>({ports:[]}));const sel=$('sign'),keep=signPicked?sel.value:'';sel.innerHTML='<option value="http://192.168.4.1">over WiFi (192.168.4.1)</option>';
 j.ports.forEach(p=>{const o=document.createElement('option');o.value=p.port;o.textContent=`over USB cable: ${p.port} (${p.kind} socket)`;sel.appendChild(o)});
 if([...sel.options].some(o=>o.value===keep))sel.value=keep;else if(j.ports.length)sel.value=j.ports[0].port}
$('rescan').onclick=async()=>{await ports();signRefresh()};$('sign').onchange=()=>{signPicked=true;signRefresh()};ports().then(signRefresh);   /* a cable wins until the user picks a target */
/* storage on the sign: every animation's share, the free space, and where the converted clip would go */
let SIGN=null,convBytes=0,sentAs='';
const cleanName=()=>$('name').value.trim().toLowerCase().replace(/[^a-z0-9_-]+/g,'-').replace(/^-+|-+$/g,'').slice(0,24);
const KB=b=>b>=1048576?(b/1048576).toFixed(2)+' MB':Math.ceil(b/1024)+' KB',pct=(b,t)=>(100*b/t).toFixed(1)+'%',hue=i=>`hsl(${(i*137.5+200)%360} 42% 55%)`;
async function signRefresh(){$('stmsg').textContent='Reading the sign…';$('strefresh').disabled=true;
 SIGN=await fetch('/api/sign-anims?sign='+encodeURIComponent($('sign').value)).then(r=>r.json()).catch(()=>({error:'no answer from the studio'}));$('strefresh').disabled=false;storage()}
function storage(){const list=$('stlist'),bar=$('stbar'),msg=$('stmsg'),fit=$('stfit');list.innerHTML='';bar.innerHTML='';fit.textContent='';fit.className='msg';
 if(!SIGN)return;if(SIGN.error){msg.innerHTML='<span class="err"></span>';msg.firstChild.textContent=SIGN.error;return}
 const A=(SIGN.anims||[]).slice().sort((a,b)=>b.bytes-a.bytes),T=SIGN.total||0,F=SIGN.free||0,used=A.reduce((n,a)=>n+a.bytes,0),name=cleanName(),sent=A.find(a=>a.id===sentAs&&a.bytes===convBytes&&sentAs===name),nb=work&&!sent?convBytes:0,old=nb?A.find(a=>a.id===name):null;
 msg.textContent=T?`${KB(T)} of storage: ${A.length} animation${A.length===1?'':'s'} use ${KB(used)} (${pct(used,T)}), ${KB(F)} free (${pct(F,T)}).`:`${A.length} animations, ${KB(used)}. Update the sign's firmware to see its free space over the cable.`;
 const seg=(w,cls,bg,title)=>{const e=document.createElement('i');e.style.width=pct(w,T);if(cls)e.className=cls;if(bg)e.style.background=bg;e.title=title;bar.appendChild(e)};
 if(T){A.forEach((a,i)=>seg(a.bytes,old&&a.id===old.id?'gone':'',hue(i),`${a.id}: ${KB(a.bytes)} (${pct(a.bytes,T)})`));
  const fs=T-F-used;if(fs>0)seg(fs,'fs','',`file system: ${KB(fs)} (${pct(fs,T)})`);
  if(nb)seg(Math.min(nb,F+(old?old.bytes:0)),'new','',`this clip: ${KB(nb)} (${pct(nb,T)})`)}
 const row=(col,nm,sz,sub,cls)=>{const li=document.createElement('li');if(cls)li.className=cls;li.innerHTML='<b></b><span class="nm"></span><span class="sz"></span><em></em>';
  li.querySelector('b').style.background=col;li.querySelector('.nm').textContent=nm;li.querySelector('.sz').textContent=sz;li.querySelector('em').textContent=sub;list.appendChild(li)};
 if(nb)row('repeating-linear-gradient(45deg,#f2a33a 0 3px,#9c5f1c 3px 6px)',`${name||'(no name yet)'} · this clip`,`${KB(nb)}${T?' · '+pct(nb,T):''}`,old?`replaces the one with this name (${KB(old.bytes)})`:'not on the sign yet','new');
 A.forEach((a,i)=>row(hue(i),a.id,`${KB(a.bytes)}${T?' · '+pct(a.bytes,T):''}`,`${a.full?'whole sign':'one light'} · ${(a.ms/1000).toFixed(1)} s · ${a.frames} frames${old&&a.id===old.id?' · will be replaced':''}`));
 if(!A.length&&!nb)list.innerHTML='<li><b></b><span class="nm" style="color:var(--muted)">No animations yet.</span></li>';
 if(nb&&T){const left=F+(old?old.bytes:0)-nb;
  if(left>=8192)fit.textContent=`Uploading this clip${name?' as "'+name+'"':''} leaves ${KB(left)} free (${pct(left,T)}).`;
  else{fit.className='msg err';fit.textContent=`This clip does not fit: ${KB(8192-left)} short. Delete or shorten an animation first.`}}}
$('strefresh').onclick=signRefresh;$('name').addEventListener('input',storage);
$('upload').onclick=async()=>{const name=$('name').value.trim().toLowerCase().replace(/[^a-z0-9_-]+/g,'-').replace(/^-+|-+$/g,'').slice(0,24);
 if(!name){$('upmsg').textContent='Give the animation a name first.';$('name').focus();return}$('name').value=name;
 $('upmsg').textContent='Checking the sign…';
 const have=await fetch('/api/sign-anims?sign='+encodeURIComponent($('sign').value)).then(r=>r.json()).catch(()=>({error:'no answer'}));
 if(have.error){$('upmsg').innerHTML='<span class="err"></span>';$('upmsg').firstChild.textContent=have.error;return}
 SIGN=have;storage();
 const old=(have.anims||[]).find(a=>a.id===name);
 if(have.total&&convBytes+8192>have.free+(old?old.bytes:0)){$('upmsg').innerHTML='<span class="err"></span>';$('upmsg').firstChild.textContent=`Not uploaded: the clip needs ${KB(convBytes)} and the sign has ${KB(have.free)} free. Delete or shorten an animation first.`;return}
 if(old&&!confirm(`The sign already has an animation called "${name}" (${Math.ceil(old.bytes/1024)} KB, ${(old.ms/1000).toFixed(1)} s). Replace it? Sets that use it will show the new one.\n\nCancel to keep it, then change the name and upload again.`)){$('upmsg').textContent='Not uploaded: change the name to keep both.';$('name').focus();return}$('upload').disabled=true;$('upmsg').textContent='Uploading…';
 const j=await withProgress('upprog',t=>fetch('/api/upload',{method:'POST',headers:{'X-Token':t},body:JSON.stringify({name,work,sign:$('sign').value})}).then(r=>r.json()).catch(e=>({error:String(e)})));$('upload').disabled=false;
 if(j.error){$('upmsg').innerHTML='<span class="err"></span>';$('upmsg').firstChild.textContent=j.error;return}
 $('upmsg').textContent=`On the sign as "${name}"${j.port?' (via '+j.port+')':''}. ${Math.floor((j.free||0)/1024)} KB left there. Pick it in a light's graphic list (reload the sign's page).`;sentAs=name;signRefresh()};
</script></body></html>"""

if __name__ == "__main__":
    if "--port" in sys.argv:
        PORT = int(sys.argv[sys.argv.index("--port") + 1])
    srv = ThreadingHTTPServer(("127.0.0.1", PORT), H)
    print(f"Animation studio on http://localhost:{PORT}/  (Ctrl+C to stop)")
    if "--no-browser" not in sys.argv:
        threading.Timer(0.8, lambda: webbrowser.open(f"http://localhost:{PORT}/")).start()
    srv.serve_forever()
