#pragma once
#include <pgmspace.h>

// Self-contained page (the AP has no internet, so no external assets).
static const char WEB_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>StopLight</title>
<style>
:root{--bg:#14171b;--panel:#1e2329;--ink:#e8ecf0;--muted:#98a4b0;--rule:#333b45;--red:#ff2814;--orange:#ff7800;--green:#1edc3c;--bad:#ff5c5c;--acc:#ffb300}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,Arial,sans-serif;padding:16px}
h1{font-size:22px;margin:0 0 4px}.sub{color:var(--muted);font-size:14px;margin:0 0 16px}
.wrap{max-width:900px;margin:0 auto;display:grid;grid-template-columns:1fr 1fr;gap:16px}
.card{background:var(--panel);border:1px solid var(--rule);border-radius:8px;padding:16px}
h2{font-size:13px;letter-spacing:.08em;text-transform:uppercase;color:var(--muted);margin:0 0 12px}
label{display:block;font-size:14px;margin:10px 0 4px}
.seg{display:inline-flex;border:1px solid var(--rule);border-radius:6px;overflow:hidden}.seg button{border:0;border-radius:0;background:transparent;padding:8px 14px}.seg button.on{background:var(--acc);color:#14171b;font-weight:600}
input[type=text],textarea,select,input[type=number]{font:inherit;padding:9px 11px;border:1px solid var(--rule);border-radius:6px;background:#0f1215;color:var(--ink)}
input[type=text],textarea{width:100%;font-size:18px}textarea{line-height:1.3;resize:vertical}
input:focus,textarea:focus,select:focus{outline:2px solid var(--acc);outline-offset:1px}
input[type=number]{width:6.5em;padding:6px 8px}
.light{border-top:1px solid var(--rule);padding:10px 0 6px}.light:first-of-type{border-top:0;padding-top:0}
.lhead{display:flex;align-items:center;gap:8px;flex-wrap:wrap}.lhead .name{font-size:14px;flex:1}.lhead select{padding:6px 8px;font-size:14px}
.swatch{display:inline-block;width:18px;height:18px;border-radius:50%;border:2px solid var(--rule);background:var(--c);cursor:pointer;padding:0}.swatch:hover{border-color:var(--acc)}.swatch.ink{width:14px;height:14px;margin-inline-start:-4px}
.swatch.rnd{background:conic-gradient(#f00,#ff0,#0f0,#0ff,#00f,#f0f,#f00)}.swatch.hj{outline:2px dashed var(--acc);outline-offset:1px}.pal .hjrow{grid-column:1/-1;display:flex;align-items:center;gap:6px;font-size:13px;color:var(--muted);padding-top:6px;border-top:1px solid var(--rule);margin-top:4px}.pal .hjrow input{width:5em}.lhead .off{padding:3px 8px;font-size:12px}.lhead .off.on{background:var(--acc);color:#14171b;border-color:var(--acc)}
.fitrow{display:flex;justify-content:space-between;align-items:center;gap:8px;margin-top:4px;flex-wrap:wrap}.fit{font-size:13px;min-height:18px;color:var(--muted)}.fit.bad{color:var(--bad)}.fit.ok{color:var(--green)}
.sz{display:flex;align-items:center;gap:6px;font-size:13px;color:var(--muted)}.sz span{min-width:6.5em;text-align:center;font-variant-numeric:tabular-nums}
.tm{display:flex;align-items:center;gap:6px;font-size:13px;color:var(--muted)}.tm input[type=checkbox]{accent-color:var(--acc);width:18px;height:18px}
button{font:inherit;padding:9px 14px;border-radius:6px;border:1px solid var(--rule);background:#2a323b;color:var(--ink);cursor:pointer}
button.primary{background:var(--acc);color:#14171b;border-color:var(--acc);font-weight:600}button:disabled{opacity:.45;cursor:not-allowed}
button.small{padding:5px 10px;font-size:13px}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-top:12px}
.frames{display:flex;gap:6px;flex-wrap:wrap;align-items:center;margin:8px 0}.frames button.cur{border-color:var(--acc);color:var(--acc)}.frames button[data-i]{touch-action:none;user-select:none;-webkit-user-select:none}.frames button.dragging{opacity:.4}.frames button.dropL{box-shadow:-4px 0 0 var(--acc)}.frames button.dropR{box-shadow:4px 0 0 var(--acc)}
.pal{position:absolute;z-index:5;background:var(--panel);border:1px solid var(--rule);border-radius:8px;padding:10px;display:grid;grid-template-columns:repeat(8,26px);gap:5px;box-shadow:0 8px 30px rgba(0,0,0,.4)}.pal button{width:26px;height:26px;border-radius:5px;border:2px solid transparent;cursor:pointer;padding:0}.pal button.on{border-color:var(--acc)}.pal .named{grid-column:1/-1;display:flex;gap:5px;flex-wrap:wrap;padding-bottom:6px;border-bottom:1px solid var(--rule);margin-bottom:4px}.pal .named button{width:auto;padding:0 8px;font-size:12px;color:#fff;text-shadow:0 0 2px #000}
ul{list-style:none;margin:0;padding:0}li{display:grid;grid-template-columns:auto 1fr auto;gap:10px;align-items:center;padding:10px 0;border-bottom:1px solid var(--rule)}
li .n{color:var(--muted);font-size:13px;width:2ch}li .t{display:grid;gap:2px}li .t span{display:block;font-size:15px;white-space:pre-line}li .t span::before{content:"";display:inline-block;width:8px;height:8px;border-radius:50%;margin-inline-end:8px;background:var(--c)}li .t small{color:var(--muted);font-size:11px;margin-inline-start:8px}
li .t em{display:block;font-style:normal;color:var(--muted);font-size:12px}li .t .nm{font-size:15px;margin-bottom:2px}li.current{background:linear-gradient(90deg,rgba(255,179,0,.12),transparent)}li .acts{display:flex;gap:6px;flex-wrap:wrap;justify-content:end}
.preview{display:grid;justify-items:center;align-content:start;gap:8px}.preview canvas{width:128px;height:384px;image-rendering:pixelated;border:1px solid var(--rule);background:#000}
.preview .live{display:flex;align-items:center;gap:8px;margin:0;font-size:14px;cursor:pointer}.preview .live input{accent-color:var(--acc);width:18px;height:18px}.preview .live.small{font-size:12px;color:var(--muted)}.preview .live.small input{width:14px;height:14px}
#pvbody{display:grid;justify-items:center;gap:6px}.preview.off{padding:10px 16px}
.msg{font-size:13px;color:var(--muted);margin-top:8px;min-height:18px}.interval{display:flex;gap:8px;align-items:center;font-size:14px;flex-wrap:wrap}
.hide{display:none!important}
.loop{display:flex;gap:6px;align-items:center;flex-wrap:wrap;font-size:13px;color:var(--muted);margin:4px 0}.loop input{width:4.5em;padding:5px 6px}.loop.bad{color:var(--bad)}
@media(max-width:720px){.wrap{grid-template-columns:1fr}.preview{order:-1;position:sticky;top:0;z-index:4;background:var(--panel);grid-template-columns:1fr auto;gap:8px 12px;align-items:center;padding:10px 12px}.preview canvas{width:64px;height:192px}#pvbody{grid-template-columns:auto;justify-items:start}.preview.off{grid-template-columns:1fr}}
</style></head><body>
<h1>StopLight</h1>
<p class="sub">A set is a sequence of frames. Build the frames, choose how long the set loops, add it, and the sign plays your sets one after another. &middot; אפשר לכתוב בעברית או באנגלית</p>
<div class="wrap">
  <div class="card" style="grid-column:1/-1">
    <h2>Sound &middot; volume</h2>
    <div class="row" style="margin-top:0"><span class="tm" style="font-size:15px">Volume <input type="range" id="vol" min="0" max="30" step="1" style="width:min(260px,60vw)"> <b id="volv"></b> / 30</span><button class="small" id="mp3Stop">Stop sound</button><button class="small" id="mp3Rescan">Rescan card</button></div>
    <div class="msg" id="mp3msg"></div>
    <div class="row"><span class="tm" style="font-size:15px">Unlit lights <select id="offStyle"><option value="ring">dim outline</option><option value="dark">completely off</option></select></span><span class="tm" style="font-size:15px">Orientation <select id="rot"><option value="0">normal</option><option value="1">rotated 180&deg;</option></select></span><span class="tm" style="font-size:15px">Panel colour wiring <select id="colrot"><option value="0">standard</option><option value="1">shifted (red&rarr;blue batch)</option></select></span></div>
    <div class="msg" id="setmsg"></div>
  </div>
  <div class="card" id="editor">
    <h2 id="edTitle">New set &middot; סט חדש</h2>
    <input type="text" id="name" maxlength="40" placeholder="Name this set (optional) · שם" autocomplete="off" style="font-size:16px;margin-bottom:10px">
    <div class="row" style="margin:0 0 10px"><span class="tm">Beat <input type="number" id="bpm" min="10" max="400" step="1" style="width:5em"> BPM &middot; tempo of the hue jumps, quarters and pulse</span></div>
    <div id="framesBar">
      <div class="frames" id="frames"></div>
      <div class="row" style="margin-top:4px"><span class="tm">Show this frame for <input type="number" id="frameMs" min="100" max="600000" step="100"> ms</span>
      <span class="tm">Sound <select id="frameMp3"><option value="0">keep playing</option><option value="-1">stop</option><option value="-2">fade out</option></select><span id="fadeRow" class="hide">over <input type="number" id="fadeS" min="0.2" max="60" step="0.5" value="3" style="width:4.5em"> s</span><button type="button" class="small" id="mp3Test" title="Play this track now">&#9654;</button></span></div>
      <div class="row" style="margin-top:6px"><button class="small" id="insBefore">+ before</button><button class="small" id="insAfter">+ after</button><button class="small" id="dupFrame">Duplicate</button><button class="small" id="delFrame">Delete frame</button></div>
    </div>
    <div id="loopRow">
      <label>Loop sections &middot; repeat a range of frames</label>
      <div id="loops"></div>
      <div class="row" style="margin-top:6px"><button class="small" id="addLoop">+ loop</button><span class="msg" id="loopmsg" style="margin:0"></span></div>
    </div>
    <div id="lights"></div>
    <div class="row"><button class="primary" id="add" disabled>Add set</button><button id="addNew" class="hide" disabled>Add as new set</button><button id="cancel" class="hide">Cancel edit</button><button id="clear">Clear</button></div>
    <div class="msg" id="msg"></div>
  </div>
  <div class="card preview" id="pvcard"><label class="live"><input type="checkbox" id="live"> Preview while editing (page + LEDs)</label>
    <div id="pvbody"><canvas id="pv" width="128" height="384"></canvas><label class="live small"><input type="checkbox" id="playAll"> play the whole set</label><div class="msg" id="livemsg"></div></div></div>
  <div class="card" style="grid-column:1/-1">
    <h2>Sets on the display</h2>
    <ul id="list"></ul>
  </div>
  <div class="card" style="grid-column:1/-1">
    <h2>Your animations</h2>
    <p class="msg" style="margin-top:0">Make <b>.anm</b> files from a GIF, a video or a YouTube link with the converter on the PC (<code>python.exe tools/anim_studio.py</code>), then upload them here or straight from the converter. Pick them in a light's graphic list.</p>
    <div class="row"><input type="file" id="animFile" accept=".anm" multiple><button class="small" id="animUp">Upload</button></div>
    <div class="msg" id="animmsg"></div>
    <div id="animuse" class="hide" style="height:6px;background:#0c0e11;border:1px solid var(--rule);border-radius:4px;overflow:hidden;margin:4px 0 8px"><i style="display:block;height:100%;width:0;background:var(--muted)"></i></div>
    <div id="animprog" class="hide" style="height:8px;background:#0c0e11;border:1px solid var(--rule);border-radius:4px;overflow:hidden;margin:6px 0"><i style="display:block;height:100%;width:0;background:var(--acc)"></i></div>
    <ul id="animList"></ul>
  </div>
</div>
<script>
const $=id=>document.getElementById(id);
const DEFC=[48,52,12],RND=64,LV=[0,140,195,255],NAMES=['Top · עליון','Middle · אמצעי','Bottom · תחתון'];
const NAMED=[['Red',48],['Orange',52],['Yellow',60],['Green',12],['Cyan',15],['Blue',3],['Magenta',51],['White',63],['Pink',53],['Lime',28]];
let SIZES=[],SPRITES=[],ANIMS=[],state={sets:[],current:0};
const FX=[   /* id, name in the graphic list, what the check line says (+ the BPM except for sparkles) */
 ['fx:quarters','Quarters · 4 colours turning','quarters: turn 30° clockwise on every beat'],['fx:pulse','Pulse · 75% ↔ full size','pulse: 75% of the circle, jumps to full size on every beat'],
 ['fx:wave','Wave · line swinging on the beat','wave: a line swinging on every beat'],['fx:clock','Clock · 5 minutes per beat','clock: starts at 12:00, 5 minutes later on every beat'],
 ['fx:ecg','Heartbeat · pulse line on the beat','heartbeat: a pulse line running right to left, its spike in the middle on every beat'],
 ['fx:ecgsweep','Heartbeat · sweep','heartbeat sweep: the trace drawn by a sweep from left to right, the spike on every beat'],
 ['fx:heart','Beating heart','heart: full size on every beat, shrinking to 80% before the next'],['fx:ripples','Ripples','ripples: a ring from the centre on every beat'],
 ['fx:eq','Equaliser','equaliser: 7 coloured bars jump on every beat'],['fx:chase','Chasing lights','chasing lights: one more bulb on every beat, then all off'],
 ['fx:ball','Bouncing ball','bouncing ball: lands on every beat'],['fx:sparkle','Sparkles','sparkles: twinkling dots, not tied to the beat'],
 ['fx:fillv','Fill · vertical lines','fill: vertical lines on every beat, full 0.1 s before the frame ends'],
 ['fx:fillx','Fill · grid lines','fill: lines down and across on every beat, full 0.1 s before the frame ends']],DEFQ=[60,12,3];
/* ---------- beat animations: mirror of rgb2Hue (Canvas.h) and Face::drawLight (Face.cpp), keep in step ---------- */
function hueTurn(c,deg){let r=(c>>4)&3,g=(c>>2)&3,b=c&3;const mx=Math.max(r,g,b),mn=Math.min(r,g,b),d=mx-mn;if(!d||deg%360===0)return c;
 let h=mx===r?(g>=b?60*(g-b)/d:360-60*(b-g)/d):mx===g?120+60*(b-r)/d:240+60*(r-g)/d;h=((h+deg)%360+360)%360;
 const up=mn+Math.floor((h%60*d+30)/60),dn=mn+Math.floor(((60-h%60)*d+30)/60);[r,g,b]=[[mx,up,mn],[dn,mx,mn],[mn,mx,up],[mn,dn,mx],[up,mn,mx],[mx,mn,dn]][Math.floor(h/60)];return(r<<4)|(g<<2)|b}
function beatOf(bpm,grid,frame,len){return{bpm,st:bpm?Math.floor(grid*bpm/60000)-Math.floor(Math.max(0,grid-frame)*bpm/60000):0,ph:bpm?(grid*bpm)%60000:0,gr:grid,fr:frame,fl:len||0}}   /* Face::Beat; fl = the frame's length */
const hueDeg=(st,per)=>(st*per)%360;
function pulseD(b){if(!b.bpm)return 96;const r=60000-b.ph;return 96+Math.floor((32*r*r+1800000000)/3600000000)}
const pvRgb=c=>c?rgbOf(c):[12,12,12];   /* the server's preview shows black as 12,12,12 (Preview.cpp) */
function beatColours(l,r,b){const bg=l.hc?hueTurn(r.c,hueDeg(b.st,l.hc)):r.c,fg=l.hf?hueTurn(r.f,hueDeg(b.st,l.hf)):r.f;return{bg,fg}}
/* ---------- uploaded animations ---------- */
function animList(free,total){const ul=$('animList');ul.innerHTML='';const pct=b=>total?(100*b/total).toFixed(b*100<total?1:0)+'%':'';
 ANIMS.forEach(a=>{const li=document.createElement('li');li.innerHTML=`<span class="n"></span><span class="t"><strong></strong><em></em></span><span class="acts"><button class="small" data-r>Rename</button><button class="small" data-d>Delete</button></span>`;
  li.querySelector('strong').textContent=a.id;li.querySelector('em').textContent=`${a.full?'whole sign':'one light'} · ${a.frames} frames · ${(a.ms/1000).toFixed(1)} s · ${Math.ceil(a.bytes/1024)} KB${total?' = '+pct(a.bytes)+' of the space':''}`;
  li.querySelector('[data-d]').onclick=async()=>{if(!confirm('Delete animation '+a.id+'? Sets that use it will show an empty circle.'))return;await fetch('/api/anims/'+a.id,{method:'DELETE'}).catch(()=>{});lastState='';load()};
  li.querySelector('[data-r]').onclick=async()=>{let n=prompt('New name for '+a.id+' (a-z, 0-9, - and _). Sets that use it are updated.',a.id);if(n===null)return;
   n=n.trim().toLowerCase().replace(/[ .]+/g,'-').replace(/[^a-z0-9_-]/g,'').slice(0,24);if(!n||n===a.id)return;
   if(ANIMS.some(x=>x.id===n)){alert('There is already an animation called '+n+'.');return}
   const r=await fetch('/api/anims/'+a.id+'/rename',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name:n})}).then(r=>r.json()).catch(()=>({error:'no connection'}));
   if(r.error){alert('Rename failed: '+r.error);return}
   ed.frames.forEach(f=>f.lights.forEach(l=>{if(l.k==='u:'+a.id)l.k='u:'+n}));persist();delete ANIMDATA[a.id];lastState='';load()};ul.appendChild(li)});
 if(!ANIMS.length)ul.innerHTML='<li><span class="n"></span><span style="color:var(--muted)">None yet.</span><span></span></li>';
 if(free!==undefined)$('animmsg').textContent=Math.floor(free/1024)+' KB free on the sign'+(total?` of ${Math.floor(total/1024)} KB (${pct(total-free)} used)`:'');if(total){$('animuse').classList.remove('hide');$('animuse').firstElementChild.style.width=(100*(total-free)/total).toFixed(1)+'%'}}
$('animUp').onclick=async()=>{const files=[...$('animFile').files];if(!files.length)return;
 const clash=files.map(f=>f.name.replace(/\.anm$/i,'').toLowerCase().replace(/[ .]+/g,'-').replace(/[^a-z0-9_-]/g,'').slice(0,24)).filter(n=>ANIMS.some(a=>a.id===n));
 if(clash.length&&!confirm('Replace '+clash.join(', ')+' on the sign? The current '+(clash.length>1?'files are':'file is')+' overwritten, and sets that use '+(clash.length>1?'them':'it')+' will show the new one.'))return;
 const bar=$('animprog'),fill=bar.firstElementChild;
 for(const f of files){const fd=new FormData();fd.append('file',f,f.name);bar.classList.remove('hide');fill.style.width='0';
  const res=await new Promise(done=>{const x=new XMLHttpRequest();x.open('POST','/api/anims?name='+encodeURIComponent(f.name.replace(/\.anm$/i,''))+'&size='+f.size);
   x.upload.onprogress=e=>{if(e.lengthComputable){const p=Math.round(100*e.loaded/e.total);fill.style.width=p+'%';$('animmsg').textContent=`Uploading ${f.name}: ${Math.round(e.loaded/1024)} of ${Math.round(e.total/1024)} KB (${p}%)`}};
   x.onload=()=>{let j={};try{j=JSON.parse(x.responseText)}catch(e){}done(x.status===200?{ok:true}:{error:j.error||('HTTP '+x.status)})};x.onerror=()=>done({error:'no connection'});x.send(fd)});
  bar.classList.add('hide');if(res.error){$('animmsg').textContent='Upload of '+f.name+' failed: '+res.error;return}}
 $('animFile').value='';lastState='';load()};
const ANIMDATA={};   /* id -> {w,h,frames:[Uint8Array],delays,total} decoded for the preview */
async function animData(id){if(ANIMDATA[id]!==undefined)return ANIMDATA[id];ANIMDATA[id]=null;
 let st=0;const b=await fetch('/api/anims/'+id+'.anm').then(r=>{st=r.status;return r.ok?r.arrayBuffer():null}).catch(()=>null);
 if(!b){setTimeout(()=>{if(ANIMDATA[id]===null)delete ANIMDATA[id]},st===404?30000:3000);return null}   /* missing or no connection: ask again later, not on every frame */
 if(new Uint8Array(b)[3]===3){try{const d=decodeV3(b);return ANIMDATA[id]=d}catch(e){delete ANIMDATA[id];return null}}
 const v=new DataView(b),w=v.getUint16(4,true),h=v.getUint16(6,true),n=v.getUint16(8,true),delays=[],lens=[];
 for(let i=0;i<n;i++){delays.push(Math.max(20,v.getUint16(12+i*6,true)));lens.push(v.getUint32(14+i*6,true))}
 const u=new Uint8Array(b),frames=[],ver=u[3];let o=12+6*n;
 for(let i=0;i<n;i++){const end=o+lens[i];let px;
  if(ver===2){px=await inflateRaw(u.subarray(o,end),w*h)}
  else{px=new Uint8Array(w*h);let p=0;while(o<end&&p<px.length){px.fill(u[o+1],p,Math.min(px.length,p+u[o]));p+=u[o];o+=2}}
  o=end;frames.push(px)}
 return ANIMDATA[id]={w,h,frames,delays,total:delays.reduce((a,d)=>a+d,0)}}
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
function animFrameIn(a,ms,from,len){if(a.frames.length<2||!a.total)return 0;if(from>=a.total)from=0;if(!len||from+len>a.total)len=a.total-from;let t=from+ms%len;for(let i=0;i<a.delays.length;i++){if(t<a.delays[i])return i;t-=a.delays[i]}return 0}
function paintAnims(e,ms,b){const L=e.L;let full=-1;L.forEach((l,i)=>{if(full<0&&l.k.startsWith('u:')){const a=ANIMDATA[l.k.slice(2)];if(a&&a.h>a.w)full=i}});
 L.forEach((l,i)=>{if(!l.k.startsWith('u:'))return;const a=ANIMDATA[l.k.slice(2)];if(a===undefined){animData(l.k.slice(2));return}if(!a)return;
  if(full>=0&&i!==full)return;if(full<0&&!((e.m>>i)&1))return;
  const r=roll(e.fi,i,l),bc=beatColours(l,r,b||beatOf(0,0,0)),fg=rgbOf(bc.fg),bg=rgbOf(bc.bg),px=a.frames[animFrameIn(a,ms,l.af||0,l.al||0)],y0=full>=0?0:i*128;
  const img=ctx.getImageData(0,y0,a.w,a.h),d=img.data;
  for(let y=0;y<a.h;y++)for(let x=0;x<a.w;x++){if(full<0){const dx=2*x-127,dy=2*y-127;if(dx*dx+dy*dy>16384)continue}
   const v=px[y*a.w+x],k=(y*a.w+x)*4;let c;if(!l.gm)c=[LV[(v>>4)&3],LV[(v>>2)&3],LV[v&3]];else c=(((v&64)!==0)!==(l.gm===2))?fg:bg;d[k]=c[0];d[k+1]=c[1];d[k+2]=c[2]}
  ctx.putImageData(img,0,y0)})}
const rgbOf=c=>[LV[(c>>4)&3],LV[(c>>2)&3],LV[c&3]];
const css=c=>c===RND?'':`rgb(${LV[(c>>4)&3]},${LV[(c>>2)&3]},${LV[c&3]})`;
const recent=[0,0];const rnd=avoid=>{let c;for(let t=0;t<32;t++){c=1+Math.floor(Math.random()*63);if(c!==avoid&&c!==recent[0]&&c!==recent[1])break}recent[1]=recent[0];recent[0]=c;return c};
const newLight=i=>({k:'short',t:'',s:0,c:DEFC[i],f:0});
const newFrame=()=>({lights:[0,1,2].map(newLight),ms:1000,mp3:0});
let MP3={present:false,tracks:0,volume:20};
function mp3Options(){const sel=$('frameMp3'),cur=ed.frames[ed.cur].mp3||0;sel.innerHTML='<option value="0">keep playing</option><option value="-1">stop</option><option value="-2">fade out</option>';$('fadeRow').classList.toggle('hide',cur!==-2);$('fadeS').value=ed.frames[ed.cur].fade||3;
 const n=Math.max(MP3.tracks,cur>0?cur:0);for(let i=1;i<=n;i++){const o=document.createElement('option');o.value=i;o.textContent='track '+String(i).padStart(3,'0')+'.mp3';sel.appendChild(o)}
 if(cur>0&&cur>MP3.tracks){sel.options[sel.options.length-1].textContent+=' (not on card)'}sel.value=String(cur);$('mp3Test').disabled=!(cur>0)}
const MAXF=64;
let ed={name:'',bpm:120,frames:[newFrame()],cur:0,loops:[],editing:null};
try{const s=JSON.parse(localStorage.getItem('stoplight.editor')||'null');if(s&&s.frames&&s.frames.length)ed={...ed,name:s.name||'',bpm:s.bpm||120,frames:s.frames,cur:Math.min(s.cur||0,s.frames.length-1),loops:s.loops||[]}}catch(e){}
function persist(){try{localStorage.setItem('stoplight.editor',JSON.stringify(ed))}catch(e){}}
function debounce(fn,ms){let h;return(...a)=>{clearTimeout(h);h=setTimeout(()=>fn(...a),ms)}}
const cur=()=>ed.frames[ed.cur].lights;
const fits=[true,true,true],eff=[0,0,0],maxFit=[0,0,0];

/* ---------- editor rendering ---------- */
function buildLights(){const L=cur(),host=$('lights');host.innerHTML='';
 L.forEach((l,i)=>{const d=document.createElement('div');d.className='light';
  d.innerHTML=`<div class="lhead"><button type="button" class="swatch" id="sw${i}" title="Circle colour"></button><button type="button" class="swatch ink" id="fw${i}" title="Text / figure colour"></button><button type="button" class="small off" id="off${i}" title="Light off (dark)">Off</button><span class="name">${NAMES[i]}</span><select id="k${i}"><option value="short">Short text</option><option value="long">Long text</option><option value="off">Off · light dark</option></select></div>
   <input type="text" id="t${i}" dir="auto" maxlength="240" autocomplete="off" placeholder="text"><textarea id="l${i}" dir="auto" maxlength="240" rows="5" class="hide"></textarea>
   <div class="fitrow"><div class="fit" id="f${i}"></div><div class="sz" id="szrow${i}"><button class="small" id="dn${i}" title="Smaller">A&minus;</button><span id="sz${i}"></span><button class="small" id="up${i}" title="Larger">A+</button></div>
</div>`;
  host.appendChild(d);
  const sel=$('k'+i);SPRITES.forEach(sp=>{const o=document.createElement('option');o.value=sp.id;o.textContent=sp.name;sel.appendChild(o)});
  {const g=document.createElement('optgroup');g.label='On the beat';FX.forEach(([v,n])=>{const o=document.createElement('option');o.value=v;o.textContent=n;g.appendChild(o)});sel.appendChild(g)}
  const qr=document.createElement('div');qr.className='row tm';qr.id='qr'+i;qr.innerHTML=`Quarters, clockwise from the top left: <button type="button" class="swatch" id="qc${i}" title="Quarter 1 = circle colour"></button>`+[0,1,2].map(k=>`<button type="button" class="swatch" id="q${i}_${k}" title="Quarter ${k+2}"></button>`).join('')+' · they turn 30° clockwise on every beat';d.appendChild(qr);
  $('qc'+i).onclick=e=>{e.preventDefault();e.stopPropagation();openPal(i,'bg')};[0,1,2].forEach(k=>{$('q'+i+'_'+k).onclick=e=>{e.preventDefault();e.stopPropagation();openPal(i,'q'+k)}});
  if(ANIMS.length){const g=document.createElement('optgroup');g.label='Your animations';ANIMS.forEach(a=>{const o=document.createElement('option');o.value='u:'+a.id;o.textContent=a.id+(a.full?' (whole sign)':'');g.appendChild(o)});sel.appendChild(g)}
  if(l.k.startsWith('u:')&&!ANIMS.find(a=>'u:'+a.id===l.k)){const o=document.createElement('option');o.value=l.k;o.textContent=l.k.slice(2)+' (missing)';sel.appendChild(o)}
  const ar=document.createElement('div');ar.className='row animrow';ar.id='ar'+i;ar.innerHTML=`<select id="gm${i}"><option value="0">original colours</option><option value="1">silhouette in light colours</option><option value="2">silhouette inverted</option></select><span class="tm">play from <input type="number" id="af${i}" min="0" step="0.1" style="width:4.5em"> s for <input type="number" id="al${i}" min="0" step="0.1" style="width:4.5em" placeholder="all"> s</span>`;d.appendChild(ar);
  $('gm'+i).value=l.gm||0;$('af'+i).value=l.af?(l.af/1000):'';$('al'+i).value=l.al?(l.al/1000):'';
  $('gm'+i).onchange=()=>{l.gm=+$('gm'+i).value;persist();preview()};
  const trim=()=>{l.af=Math.max(0,Math.round((+$('af'+i).value||0)*1000));l.al=Math.max(0,Math.round((+$('al'+i).value||0)*1000));persist();preview()};$('af'+i).onchange=trim;$('al'+i).onchange=trim;
  sel.value=l.k;$('t'+i).value=l.k==='short'?l.t:'';$('l'+i).value=l.k==='long'?l.t:'';
  const setKind=k=>{const prev=l.k;l.k=k;sel.value=k;if(l.k==='short'&&prev==='long')l.t=l.t.replace(/\s*\n\s*/g,' ');if(l.k==='short')$('t'+i).value=l.t;if(l.k==='long')$('l'+i).value=l.t;showBoxes(i);changed(i)};
  sel.onchange=()=>setKind(sel.value);$('off'+i).onclick=()=>setKind(l.k==='off'?'short':'off');
  const c=debounce(()=>changed(i),200);
  $('t'+i).addEventListener('input',()=>{l.t=$('t'+i).value;c()});$('l'+i).addEventListener('input',()=>{l.t=$('l'+i).value;c()});
  $('sw'+i).onclick=e=>{e.preventDefault();e.stopPropagation();openPal(i,'bg')};$('fw'+i).onclick=e=>{e.preventDefault();e.stopPropagation();openPal(i,'ink')};
  $('up'+i).onclick=()=>step(i,1);$('dn'+i).onclick=()=>step(i,-1);
  swatchUI(i);showBoxes(i)});
 {const fb=$('frames');fb.innerHTML='';ed.frames.forEach((f,i)=>{const b=document.createElement('button');b.type='button';b.className='small'+(i===ed.cur?' cur':'');b.textContent=(i+1);b.dataset.i=i;b.title='Frame '+(i+1)+' · drag to move it';b.onclick=()=>{if(dragDone){dragDone=false;return}ed.cur=i;persist();buildLights();refit()};fb.appendChild(b)});
  const add=document.createElement('button');add.type='button';add.className='small';add.textContent='+ at end';add.disabled=ed.frames.length>=MAXF;add.onclick=()=>insertFrame(ed.frames.length);fb.appendChild(add);
  $('frameMs').value=ed.frames[ed.cur].ms;mp3Options();buildLoops()}
 $('name').value=ed.name||'';$('bpm').value=ed.bpm||120;$('add').textContent=ed.editing===null?'Add set':'Save changes';$('addNew').classList.toggle('hide',ed.editing===null);$('cancel').classList.toggle('hide',ed.editing===null);$('edTitle').textContent=ed.editing===null?'New set · סט חדש':'Editing set '+(ed.editing+1)+(ed.name?' · '+ed.name:'')}
function showBoxes(i){const l=cur()[i],g=l.k!=='short'&&l.k!=='long';$('ar'+i).classList.toggle('hide',!l.k.startsWith('u:'));$('qr'+i).classList.toggle('hide',l.k!=='fx:quarters');$('t'+i).classList.toggle('hide',l.k!=='short');$('l'+i).classList.toggle('hide',l.k!=='long');$('szrow'+i).classList.toggle('hide',g);$('off'+i).classList.toggle('on',l.k==='off')}
function swatchUI(i){const l=cur()[i];const sw=$('sw'+i),fw=$('fw'+i);sw.classList.toggle('rnd',l.c===RND);fw.classList.toggle('rnd',l.f===RND);sw.style.setProperty('--c',css(l.c));fw.style.setProperty('--c',css(l.f));
 sw.classList.toggle('hj',!!l.hc);fw.classList.toggle('hj',!!l.hf);sw.title='Circle colour'+(l.hc?`, turning ${l.hc}° on every beat`:'');fw.title='Text / figure colour'+(l.hf?`, turning ${l.hf}° on every beat`:'');
 const q=l.q||DEFQ,qc=$('qc'+i);qc.classList.toggle('rnd',l.c===RND);qc.style.setProperty('--c',css(l.c));[0,1,2].forEach(k=>{const b=$('q'+i+'_'+k);b.classList.toggle('rnd',q[k]===RND);b.style.setProperty('--c',css(q[k]))})}
function openPal(i,which){document.querySelectorAll('.pal').forEach(p=>p.remove());const l=cur()[i],pal=document.createElement('div');pal.className='pal';const v=which==='ink'?l.f:which==='bg'?l.c:(l.q||DEFQ)[+which[1]];
 const named=document.createElement('div');named.className='named';
 const mk=(n,c,bg)=>{const b=document.createElement('button');b.type='button';b.textContent=n;b.style.background=bg;b.onclick=()=>pick(i,c,which);if(c===v)b.className='on';named.appendChild(b)};
 mk('Random',RND,'conic-gradient(#f00,#ff0,#0f0,#0ff,#00f,#f0f,#f00)');mk('Black',0,'#000');NAMED.forEach(([n,c])=>mk(n,c,css(c)));pal.appendChild(named);
 for(let c=0;c<64;c++){const b=document.createElement('button');b.type='button';b.style.background=css(c);b.title='rgb '+((c>>4)&3)+((c>>2)&3)+(c&3);if(c===v)b.className='on';b.onclick=()=>pick(i,c,which);pal.appendChild(b)}
 if(which==='bg'||which==='ink'){const key=which==='ink'?'hf':'hc',h=document.createElement('label');h.className='hjrow';h.innerHTML=`Hue jump <input type="number" step="10" min="-350" max="350"> ° on every beat (0 = steady; + turns red→yellow→green)`;
  const inp=h.querySelector('input');inp.value=l[key]||0;inp.onchange=()=>{l[key]=Math.max(-359,Math.min(359,Math.round(+inp.value||0)));inp.value=l[key];swatchUI(i);persist();preview()};pal.appendChild(h)}
 const sw=$((which==='ink'?'fw':which==='bg'?'sw':'q'+i+'_')+(which[0]==='q'?which[1]:i)),r=sw.getBoundingClientRect();pal.style.left=Math.min(r.left+window.scrollX,window.innerWidth-260)+'px';pal.style.top=(r.bottom+window.scrollY+6)+'px';document.body.appendChild(pal);
 setTimeout(()=>document.addEventListener('click',function h(e){if(!pal.contains(e.target)){pal.remove();document.removeEventListener('click',h)}}),0)}
function pick(i,c,which){const l=cur()[i];if(which==='ink')l.f=c;else if(which==='bg')l.c=c;else{l.q=[...(l.q||DEFQ)];l.q[+which[1]]=c}swatchUI(i);document.querySelectorAll('.pal').forEach(p=>p.remove());persist();preview()}

/* ---------- fit checks and sizes ---------- */
async function checkLight(i){const l=cur()[i];if(l.k==='off'){fits[i]=true;F(i,'','light off');sizeUI(i);update();return}if(l.k.startsWith('u:')){const a=ANIMS.find(x=>'u:'+x.id===l.k);fits[i]=!!a;F(i,a?'ok':'bad',a?`animation ${a.id}: ${(a.ms/1000).toFixed(1)} s, ${a.frames} frames${a.full?' · fills the whole sign':''}`:'animation not on the sign');sizeUI(i);update();return}
 const fxe=FX.find(x=>x[0]===l.k);if(fxe){fits[i]=true;F(i,'ok',fxe[2]+(l.k==='fx:sparkle'?'':` (${ed.bpm} BPM)`));sizeUI(i);update();return}
 if(l.k!=='short'&&l.k!=='long'){fits[i]=true;F(i,'ok','graphic: '+((SPRITES.find(s=>s.id===l.k)||{}).name||l.k));sizeUI(i);update();return}
 const v=l.t.trim();if(!v){fits[i]=true;F(i,'','');eff[i]=0;sizeUI(i);update();return}
 const r=await fetch(`/api/check?text=${encodeURIComponent(v)}&light=${i}&size=${l.s}`).then(r=>r.json()).catch(()=>({ok:false,error:'no connection'}));
 if(!r.ok&&l.s&&r.maxFit){l.s=0;return checkLight(i)}
 fits[i]=!!r.ok;eff[i]=r.size||0;maxFit[i]=r.maxFit||0;F(i,r.ok?'ok':'bad',r.ok?`fits: ${r.lines} line${r.lines>1?'s':''}`:'does not fit: '+r.error);sizeUI(i);update()}
function F(i,cls,txt){const f=$('f'+i);if(!f)return;f.className='fit '+cls;f.textContent=txt}
function sizeUI(i){const l=cur()[i],ok=fits[i]&&(l.k==='short'||l.k==='long')&&l.t.trim(),min=SIZES.length?Math.min(...SIZES):0;$('sz'+i).textContent=ok?`size ${eff[i]}${l.s?'':' (auto)'}`:'';$('up'+i).disabled=!ok||eff[i]>=maxFit[i];$('dn'+i).disabled=!ok||eff[i]<=min}
function step(i,dir){const l=cur()[i];if(!fits[i])return;const s=dir>0?SIZES.filter(x=>x>eff[i]).sort((a,b)=>a-b)[0]:SIZES.filter(x=>x<eff[i]).sort((a,b)=>b-a)[0];if(s===undefined)return;l.s=(dir>0&&s>=maxFit[i])?0:s;persist();checkLight(i);preview()}
function changed(i){persist();checkLight(i);preview()}
function refit(){[0,1,2].forEach(checkLight);preview()}
function update(){const bad=!(fits[0]&&fits[1]&&fits[2])||loopErrors().some(Boolean);$('add').disabled=bad;$('addNew').disabled=bad}

/* ---------- frames / loops ---------- */
function loopErrors(){const n=ed.frames.length,errs=ed.loops.map(()=>'');ed.loops.forEach((L,i)=>{if(!(L.from>=1&&L.to>=L.from&&L.to<=n))errs[i]='must be inside 1–'+n;else if(L.count<1)errs[i]='count';else ed.loops.forEach((M,j)=>{if(j!==i&&!errs[i]&&!(L.to<M.from||L.from>M.to))errs[i]='overlaps another loop'})});return errs}
function buildLoops(){const host=$('loops');host.innerHTML='';const errs=loopErrors();
 ed.loops.forEach((L,i)=>{const d=document.createElement('div');d.className='loop'+(errs[i]?' bad':'');d.innerHTML=`frames <input type="number" min="1" max="${ed.frames.length}" value="${L.from}"> to <input type="number" min="1" max="${ed.frames.length}" value="${L.to}"> × <input type="number" min="1" max="1000" value="${L.count}"> times <button type="button" class="small">remove</button><span>${errs[i]||''}</span>`;
  const [a,b,c]=d.querySelectorAll('input');a.onchange=()=>{L.from=+a.value||1;persist();buildLoops();preview()};b.onchange=()=>{L.to=+b.value||1;persist();buildLoops();preview()};c.onchange=()=>{L.count=+c.value||1;persist();buildLoops();preview()};
  d.querySelector('button').onclick=()=>{ed.loops.splice(i,1);persist();buildLoops();preview()};host.appendChild(d)});
 $('loopmsg').textContent=ed.loops.length?'':'Without loops the frames play once, then the next set starts.';update()}
$('addLoop').onclick=()=>{if(ed.loops.length>=16)return;const n=ed.frames.length;ed.loops.push({from:Math.min(ed.cur+1,n),to:n,count:2});persist();buildLoops();preview()};
$('frameMs').onchange=()=>{ed.frames[ed.cur].ms=+$('frameMs').value||1000;persist();preview()};
$('frameMp3').onchange=()=>{ed.frames[ed.cur].mp3=+$('frameMp3').value||0;persist();mp3Options()};
$('fadeS').onchange=()=>{ed.frames[ed.cur].fade=Math.max(0.2,Math.min(60,+$('fadeS').value||3));persist()};
$('mp3Test').onclick=()=>{const t=ed.frames[ed.cur].mp3;if(t>0)fetch('/api/mp3/play/'+t,{method:'POST'}).then(r=>r.json()).then(render).catch(()=>{})};
$('mp3Stop').onclick=()=>fetch('/api/mp3/stop',{method:'POST'}).then(r=>r.json()).then(render).catch(()=>{});
$('mp3Rescan').onclick=()=>fetch('/api/mp3/rescan',{method:'POST'}).then(r=>r.json()).then(render).catch(()=>{});
$('vol').oninput=()=>{$('volv').textContent=$('vol').value};
$('offStyle').onchange=()=>fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({offDark:$('offStyle').value==='dark'})}).then(r=>r.json()).then(s=>{render(s);preview()}).catch(()=>{});
$('colrot').onchange=()=>{$('setmsg').textContent='Saving and restarting the sign…';fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({colourRotated:$('colrot').value==='1'})}).catch(()=>{});setTimeout(()=>{$('setmsg').textContent='';lastState='';load()},6000)};
$('rot').onchange=()=>fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({rotate180:$('rot').value==='1'})}).then(r=>r.json()).then(render).catch(()=>{});
$('vol').onchange=()=>fetch('/api/mp3/volume',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({volume:+$('vol').value})}).then(r=>r.json()).then(render).catch(()=>{});
// Insert a blank frame at `at`; loop sections at or beyond it shift to keep pointing at the same frames.
function insertFrame(at,copy){if(ed.frames.length>=MAXF)return;ed.frames.splice(at,0,copy?JSON.parse(JSON.stringify(ed.frames[ed.cur])):newFrame());
 ed.loops.forEach(L=>{if(L.from>at)L.from++;if(L.to>=at+1&&L.to>=L.from)L.to++;if(L.from>at&&L.to<L.from)L.to=L.from});ed.cur=at;persist();buildLights();refit()}
// Move frame a to position b (0-based). Loop sections follow their frames, as for a delete then an insert.
function moveFrame(a,b){if(a===b)return;const f=ed.frames.splice(a,1)[0];
 ed.loops=ed.loops.map(L=>({from:L.from>a+1?L.from-1:L.from,to:L.to>a?L.to-1:L.to,count:L.count})).filter(L=>L.to>=L.from);
 ed.frames.splice(b,0,f);ed.loops.forEach(L=>{if(L.from>b)L.from++;if(L.to>=b+1&&L.to>=L.from)L.to++;if(L.from>b&&L.to<L.from)L.to=L.from});
 ed.cur=b;persist();buildLights();refit()}
/* Drag a frame number onto another to move it there (pointer events: mouse and touch alike). */
let drag=null,dragDone=false;
{const fb=$('frames'),btnAt=(x,y)=>{const el=document.elementFromPoint(x,y);return el&&el.closest?el.closest('#frames button[data-i]'):null},
  clear=()=>fb.querySelectorAll('button').forEach(b=>b.classList.remove('dragging','dropL','dropR'));
 fb.addEventListener('pointerdown',e=>{const b=e.target.closest('button[data-i]');if(!b||e.button>0)return;drag={from:+b.dataset.i,x:e.clientX,y:e.clientY,on:false,to:null};b.setPointerCapture(e.pointerId)});
 fb.addEventListener('pointermove',e=>{if(!drag)return;if(!drag.on&&Math.hypot(e.clientX-drag.x,e.clientY-drag.y)<6)return;drag.on=true;clear();
  fb.querySelector(`button[data-i="${drag.from}"]`).classList.add('dragging');const t=btnAt(e.clientX,e.clientY);drag.to=t?+t.dataset.i:null;
  if(t&&drag.to!==drag.from)t.classList.add(drag.to>drag.from?'dropR':'dropL')});
 const end=e=>{if(!drag)return;const d=drag;drag=null;clear();if(!d.on)return;dragDone=true;setTimeout(()=>{dragDone=false},300);if(e.type==='pointerup'&&d.to!==null&&d.to!==d.from)moveFrame(d.from,d.to)};
 fb.addEventListener('pointerup',end);fb.addEventListener('pointercancel',end)}
$('insBefore').onclick=()=>insertFrame(ed.cur);$('insAfter').onclick=()=>insertFrame(ed.cur+1);
$('dupFrame').onclick=()=>insertFrame(ed.cur+1,true);
$('delFrame').onclick=()=>{if(ed.frames.length<=1)return;const at=ed.cur;ed.frames.splice(at,1);
 ed.loops=ed.loops.map(L=>({from:L.from>at+1?L.from-1:L.from,to:L.to>at?L.to-1:L.to,count:L.count})).filter(L=>L.to>=L.from);ed.cur=Math.max(0,at-1);persist();buildLights();refit()};
$('name').addEventListener('input',()=>{ed.name=$('name').value;persist()});
$('bpm').onchange=()=>{ed.bpm=Math.max(10,Math.min(400,Math.round(+$('bpm').value||120)));$('bpm').value=ed.bpm;persist();refit()};

/* ---------- what gets sent ---------- */
const isText=l=>l.k==='short'||l.k==='long';
function lightsJson(L){return{t:L.map(l=>isText(l)?l.t.trim():''),k:L.map(l=>l.k==='long'?'long':l.k==='off'?'off':'short'),g:L.map(l=>(isText(l)||l.k==='off')?'':l.k),s:L.map(l=>l.s),c:L.map(l=>l.c),f:L.map(l=>l.f),gm:L.map(l=>l.gm||0),af:L.map(l=>l.af||0),al:L.map(l=>l.al||0),hc:L.map(l=>l.hc||0),hf:L.map(l=>l.hf||0),q:L.map(l=>l.q||DEFQ)}}
const maskOf=L=>L.reduce((m,l,i)=>m|(l.k!=='off'?1<<i:0),0);
function setJson(){const name=(ed.name||'').trim().slice(0,40);return{name,bpm:ed.bpm||120,frames:ed.frames.map(f=>({...lightsJson(f.lights),m:maskOf(f.lights),ms:f.ms,mp3:f.mp3||0,...(f.mp3===-2?{fade:f.fade||3}:{})})),loops:ed.loops.map(L=>({from:L.from,to:L.to,count:L.count}))}}
function playlist(frames,loops){const out=[],n=frames.length;let i=0;while(i<n&&out.length<400){const L=loops.find(x=>x.from-1===i&&x.to<=n&&x.to>=x.from);if(L){for(let c=0;c<L.count&&out.length<400;c++)for(let k=i;k<L.to;k++)out.push(k);i=L.to}else{out.push(i);i++}}return out}
function draftJson(){const L=cur();return{...lightsJson(L),m:maskOf(L),bpm:ed.bpm||120,ms:ed.frames[ed.cur].ms}}

/* ---------- preview: timeline of {lights, mask, ms} played on a canvas ---------- */
const ctx=$('pv').getContext('2d');let frames={},fails={},animTimer=null,animGen=0,rolls={};
function decodeRle(buf){const b=new Uint8Array(buf);if(b.length<7||b[0]!==82||b[1]!==76)return null;const w=b[2]|(b[3]<<8),h=b[4]|(b[5]<<8),n=b[6];const pal=[];for(let i=0;i<n;i++)pal.push([b[7+i*3],b[8+i*3],b[9+i*3]]);
 const img=ctx.createImageData(w,h),d=img.data;let p=0,o=7+n*3;while(o+1<b.length){const v=b[o]|(b[o+1]<<8);o+=2;const c=pal[v>>12]||[0,0,0];let len=v&4095;while(len--&&p<d.length){d[p++]=c[0];d[p++]=c[1];d[p++]=c[2];d[p++]=255}}
 return p===d.length?img:null}   /* incomplete stream: reject, the caller refetches */
function timeline(){let t=0,b0=0;if(!$('playAll').checked){const f=ed.frames[ed.cur];return[{L:f.lights,m:maskOf(f.lights),ms:f.ms,fi:ed.cur,start:0,b0:0}]}
 const pl=loopErrors().some(Boolean)?ed.frames.map((_,i)=>i):playlist(ed.frames,ed.loops);return pl.map((fi,pi)=>{const f=ed.frames[fi];if(pi&&f.mp3>0)b0=t;const e={L:f.lights,m:maskOf(f.lights),ms:f.ms,fi:pi,start:t,b0};t+=f.ms;return e})}
/* Beat animations painted over the server's image of the frame's start: quarters, pulse, wave, clock and heartbeat lights are drawn
   whole, hue-jumping lights get their circle and ink colours swapped (Face::drawLight does the same on the LEDs). */
function paintBeat(e,b){if(e.L.some(l=>l.k.startsWith('u:')&&(ANIMDATA[l.k.slice(2)]||{}).h>128))return;
 e.L.forEach((l,i)=>{if(!((e.m>>i)&1)||l.k.startsWith('u:'))return;const fx=FXN[l.k]||0;if(!b.bpm&&!timeFx(fx))return;if(!fx&&!l.hc&&!l.hf)return;
  const r=roll(e.fi,i,l),{bg,fg}=beatColours(l,r,b),img=ctx.getImageData(0,i*128,128,128),d=img.data;
  if(fx>=6&&fx<=13){const f=rowFxPrep(fx,l,b,bg,fg===bg?(bg?0:63):fg,i),row=Array(128),pal=[];
   for(let y=0;y<128;y++){rowFxRow(f,y,row);for(let x=0;x<128;x++){const v=row[x],c=pal[v]||(pal[v]=pvRgb(v)),k=(y*128+x)*4;d[k]=c[0];d[k+1]=c[1];d[k+2]=c[2]}}}
  else if(fx){let q=Array(12).fill(bg),D=128,wv=null,wc=null,wh=null;if(fx===1){const deg=hueDeg(b.st,l.hc||0),cols=[bg,...r.q.map(v=>hueTurn(v,deg))];q=q.map((_,s)=>cols[Math.floor(((s+15-b.st%12)%12)/3)])}else if(fx===2)D=pulseD(b);else if(fx===3)wv=waveRows(waveAmp(b));else if(fx===5||fx===14){const sc=b.bpm?Math.floor(b.ph*128/60000):0,lead=(64+sc)&127,ik=fg===bg?(bg?0:63):fg,dm=dim2(ik);wv=ecgRows(fx===5?-sc:0);
    if(fx===14&&b.bpm){const di=pvRgb(dm===bg?ik:dm);wc=[];wh=[];for(let x=0;x<128;x++){const age=(lead-x)&127;wh.push(age<64);wc.push(age>=48?di:null)}}}const ck=fx===4?clockFace(b):null;
   const c4=q.map(pvRgb),blk=pvRgb(0),wi=pvRgb(fg===bg?(bg?0:63):fg);for(let y=0;y<128;y++){const dy=2*y-127;for(let x=0;x<128;x++){const dx=2*x-127,k=(y*128+x)*4,c=dx*dx+dy*dy<=D*D?(wv&&dy>=wv[0][x]&&dy<=wv[1][x]&&(!wh||wh[x])?(wc&&wc[x]||wi):ck&&clockInk(ck,dx,dy,dx*dx+dy*dy)?wi:c4[sector(dx,dy)]):blk;d[k]=c[0];d[k+1]=c[1];d[k+2]=c[2]}}}
  else{const ink=fg===bg?(bg?0:63):fg,b0=pvRgb(r.c),f0=pvRgb(r.f===r.c?(r.c?0:63):r.f),b1=pvRgb(bg),f1=pvRgb(ink);
   for(let k=0;k<d.length;k+=4){const c=d[k]===b0[0]&&d[k+1]===b0[1]&&d[k+2]===b0[2]?b1:d[k]===f0[0]&&d[k+1]===f0[1]&&d[k+2]===f0[2]?f1:null;if(c){d[k]=c[0];d[k+1]=c[1];d[k+2]=c[2]}}}
  ctx.putImageData(img,0,i*128)})}
function sector(dx,dy){const x2=dx*dx,y2=dy*dy,v=3*x2<y2?0:x2<3*y2?1:2,h=3*y2<x2?0:y2<3*x2?1:2;return dx>0?(dy<0?v:3+h):(dy>0?6+v:9+h)}   /* Face.cpp sector(): twelfths clockwise from 12 o'clock */
/* fx:wave, as Face.cpp: SIN_Q = 1024*sin over a quarter turn (256 steps a turn), the amplitude in px (+ = left up)
   and the line's doubled-unit rows per column (centre per column, then a round brush) */
const SIN_Q=[0,25,50,75,100,125,150,175,200,224,249,273,297,321,345,369,392,415,438,460,483,505,526,548,569,590,610,630,650,669,688,706,724,742,759,775,792,807,822,837,851,865,878,891,903,915,926,936,946,955,964,972,980,987,993,999,1004,1009,1013,1016,1019,1021,1023,1024,1024];
function sin256(n){n&=255;const i=n&63,q=n>>6;return q===0?SIN_Q[i]:q===1?SIN_Q[64-i]:q===2?-SIN_Q[i]:-SIN_Q[64-i]}
function waveAmp(b){if(!b.bpm)return 0;const c=sin256(64+Math.floor(b.ph*128/60000)),a=(26*Math.abs(c)+512)>>10;return(c<0)!==((b.st&1)===1)?-a:a}
function waveRows(a){const rd=v=>v<0?-((-v+512)>>10):(v+512)>>10,W=[6,5,4],mid=[],lo=[],hi=[];for(let x=-2;x<130;x++)mid.push(-rd(2*a*sin256(2*x+1)));
 for(let x=0;x<128;x++){let l=1e9,h=-1e9;for(let j=-2;j<=2;j++){const m=mid[x+2+j],w=W[Math.abs(j)];l=Math.min(l,m-w);h=Math.max(h,m+w)}lo.push(l);hi.push(h)}return[lo,hi]}
/* fx:ecg, as Face.cpp: the trace (px above the baseline per column, R spike on 63/64) scrolled s px to the right
   (fx:ecg passes -scroll: it moves right to left; fx:ecgsweep 0, seen through a window sweeping left to right);
   each column's run reaches half way to its neighbours, the line takes three columns' runs plus 3 up and down */
const ECG=[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,2,3,3,4,5,6,6,6,6,5,4,3,3,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,-1,-2,-4,-4,-1,5,15,25,35,35,25,15,5,-3,-8,-9,-6,-2,0,0,0,0,0,0,0,0,0,1,1,2,3,4,6,7,8,9,9,10,10,10,10,9,9,8,7,6,4,3,2,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0];
function ecgRows(s){const E=x=>ECG[(x-s)&127],rl=[],rh=[],lo=[],hi=[];for(let x=-1;x<=128;x++){const v=E(x),c=-2*v,l=-(v+E(x-1)),r=-(v+E(x+1));rl.push(Math.min(c,l,r));rh.push(Math.max(c,l,r))}
 for(let x=0;x<128;x++){lo.push(Math.min(rl[x],rl[x+1],rl[x+2])-3);hi.push(Math.max(rh[x],rh[x+1],rh[x+2])+3)}return[lo,hi]}
/* fx:clock, as Face.cpp: 12:00 at the frame's start, 5 minutes a beat with a quartz tick (eases out over 1/8 beat);
   angles in half degrees, directions 1024 = 1, geometry in doubled units (bezel, marks, tapered hands, hub with pin) */
function rdiv(v,d){return v<0?-Math.floor((-v+Math.floor(d/2))/d):Math.floor((v+Math.floor(d/2))/d)}
function sin720(a){const t=(((a%720)+720)%720)*256,i=Math.floor(t/720),s0=sin256(i);return s0+rdiv((sin256(i+1)-s0)*(t%720),720)}
function clockFace(b){const n=b.st;let h=n%144*5,m=n%12*60;if(n&&b.ph<7500){const r=7500-b.ph,q=56250000;h=(h+720-Math.floor((5*r*r+q/2)/q))%720;m=(m+720-Math.floor((60*r*r+q/2)/q))%720}
 const u=a=>[sin720(a),-sin720(a+180)];return{marks:[...Array(12).keys()].map(k=>u(k*60)),h:u(h),m:u(m)}}
function onHand(dx,dy,[ux,uy],tail,len,w0,w1){const al=dx*ux+dy*uy,ac=dx*uy-dy*ux,t=tail*1024,l=len*1024;
 return al<t?(al-t)**2+ac**2<=w0*w0*1048576:al<=0?Math.abs(ac)<=w0*1024:al<=l?Math.abs(ac)*len<=w0*len*1024+(w1-w0)*al:(al-l)**2+ac**2<=w1*w1*1048576}
function clockInk(f,dx,dy,d2){if(d2>=122*122)return true;if(d2<=14*14)return d2>16;
 if(d2>=84*84&&d2<=114*114){const d=sector(dx,dy);for(let k=d;k<=d+1;k++){const m=k%12,main=m%3===0,[ux,uy]=f.marks[m],al=dx*ux+dy*uy,ac=dx*uy-dy*ux,w=main?5:3;if(al>=(main?84:98)*1024&&al<=114*1024&&ac*ac<=w*w*1048576)return true}}
 return onHand(dx,dy,f.m,0,102,7,4)||onHand(dx,dy,f.h,0,64,9,5)}
/* Beat animations painted a row at a time (fx:heart and later), as Face.cpp RowFx: rowFxPrep once per draw, then
   rowFxRow per row into an array of 0bRRGGBB colours. Integer maths with the same hash (mix32) and tables. */
const SPC=[63,60,15,51,56,12,47],FXN={'fx:quarters':1,'fx:pulse':2,'fx:wave':3,'fx:clock':4,'fx:ecg':5,'fx:heart':6,'fx:ripples':7,'fx:eq':8,'fx:chase':9,'fx:ball':10,'fx:sparkle':11,'fx:fillv':12,'fx:fillx':13,'fx:ecgsweep':14},
 timeFx=fx=>fx===11||fx===12||fx===13,EQC=[48,56,60,12,15,3,51];
function mix32(x){x>>>=0;x^=x>>>16;x=Math.imul(x,0x7feb352d);x^=x>>>15;x=Math.imul(x,0x846ca68b);x^=x>>>16;return x>>>0}
const dim2=c=>((c>>4)&3?16:0)|((c>>2)&3?4:0)|(c&3?1:0);
function fillShown(b,n){const end=b.fl-100;if(!b.bpm||end<0)return b.fr>=end?n:0;const start=Math.max(0,b.gr-b.fr),
 upTo=t=>Math.floor((start+t)*b.bpm/60000)-(start?Math.floor((start-1)*b.bpm/60000):-1),beats=upTo(end);   /* beat-grid moments in [frame start, t] */
 if(!beats)return b.fr>=end?n:0;const k=Math.min(upTo(b.fr),beats);
 return beats<=n?Math.floor((k*n+beats-1)/beats):k?1+Math.floor((k-1)*(n-1)/(beats-1)):0}
function rowFxPrep(fx,l,b,base,ink,light){const f={e:fx,base,ink,dim:dim2(ink)},n=b.st;
 if(fx===6){f.z=b.bpm?205+Math.floor((51*(60000-b.ph)**2+1800000000)/3600000000):205}
 else if(fx===7){const pq=b.bpm?Math.floor(b.ph*256/60000):128,st=b.bpm?Math.min(n,2):2;f.rings=[];
  for(let j=0;j<=st;j++){const a=j*256+pq;if(a>=640)continue;const r=Math.floor(128*a*(1920-a)/819200),w=5-Math.floor(3*a/640),lo=r>w?r-w:0;f.rings.push([lo*lo,(r+w)*(r+w),a>=448&&f.dim!==base?f.dim:ink])}}
 else if(fx===8){const rem=b.bpm?60000-b.ph:60000,deg=hueDeg(n,l.hc||0);f.pk=[];f.h=[];f.col=[];
  for(let i=0;i<7;i++){const p=3+mix32(Math.imul(n,7)+i+0x5eed)%10;f.pk.push(p);f.h.push(1+Math.floor(((p-1)*rem*rem+1800000000)/3600000000));f.col.push(deg?hueTurn(EQC[i],deg):EQC[i])}
  f.bar=[];for(let x=0;x<128;x++){const t=x-10;f.bar.push(t>=0&&t<112&&t%16<12?Math.floor(t/16):-1)}}
 else if(fx===9){f.bx=[];f.by=[];for(let k=0;k<12;k++){f.bx.push(rdiv(100*sin720(k*60),1024));f.by.push(-rdiv(100*sin720(k*60+180),1024))}f.lit=(n+1)%13;if(f.dim===ink)f.dim=dim2(base)}
 else if(fx===10){const u=b.bpm?b.ph:0,d=Math.min(u,60000-u),h=d<3000?0:Math.floor(4*128*(u-3000)*(57000-u)/2916000000),sq=b.bpm&&d<3000?Math.floor((6*(3000-d)+1500)/3000):0;f.rx=22+sq;f.ry=22-sq;f.cy=97-f.ry-h}
 else if(fx===11){const t=Math.floor(b.fr/40),seed=Math.imul(light,0x9e3779b9),deg=hueDeg(n,l.hf||0),hc=c=>deg?hueTurn(c,deg):c;f.sp=[];
  for(let i=0;i<20;i++){const per=14+mix32(i*977+1+seed)%12,tt=t+mix32(i*131+7+seed)%per,g=Math.floor(tt/per),tau=tt%per,h=mix32(Math.imul(i,7919)+Math.imul(g,104729)+12345+seed),h2=mix32(h);
   let life=8+h%12;if(life>per-2)life=per-2;if(tau>=life)continue;const a=(h>>>4)%720,u1=h2&1023,u2=(h2>>>10)&1023,rho=Math.floor(54*Math.max(u1,u2)/1023);
   let ci=(h2>>>21)%7,col=hc(SPC[ci]);while(col===base){ci=(ci+1)%7;col=hc(SPC[ci])}const dm=dim2(col);
   f.sp.push({x:63+rdiv(rho*sin720(a),1024),y:63-rdiv(rho*sin720(a+180),1024),st:Math.floor(tau*4/life),star:(h2>>>20)&1,col,dim:dm!==base?dm:col})}}
 else if(fx===12||fx===13){const lines=fx===12?64:128,shown=fillShown(b,lines),o=[...Array(lines).keys()];
  for(let i=lines-1;i>0;i--){const j=mix32(lines*1000+i)%(i+1),t=o[i];o[i]=o[j];o[j]=t}
  f.col=Array(64).fill(0);f.row=Array(64).fill(0);for(let i=0;i<shown;i++){if(o[i]<64)f.col[o[i]]=1;else f.row[o[i]-64]=1}}
 return f}
function rowFxRow(f,y,row){const dy=2*y-127,dy2=dy*dy,F=16384,e=f.e;
 if(e===6){const z=f.z,py=dy*256,a=Math.abs(py-10*z),ly=py+37*z,ly2=ly*ly,hz=94*z,lx=47*z,r2=4418*z*z,col=f.base?f.base:f.ink;
  for(let x=0;x<128;x++){const dx=2*x-127,ax=Math.abs(dx)*256,ex=ax-lx;row[x]=dx*dx+dy2<=F&&(ax+a<=hz||ex*ex+ly2<=r2)?col:0}}
 else if(e===7){for(let x=0;x<128;x++){const dx=2*x-127,d2=dx*dx+dy2;let v=d2<=F?f.base:0;for(const g of f.rings)if(d2>=g[0]&&d2<=g[1]){v=g[2];break}row[x]=d2<=F?v:0}}
 else if(e===8){const u=107-y,seg=u>=0&&u%8<6&&Math.floor(u/8)<12?Math.floor(u/8):-1,rim2=120*120;
  for(let x=0;x<128;x++){const dx=2*x-127,d2=dx*dx+dy2,bar=f.bar[x];row[x]=d2>F?0:d2>=rim2?f.base:bar>=0&&seg>=0&&(seg<f.h[bar]||seg===f.pk[bar]-1)?f.col[bar]:0}}
 else if(e===9){const lo=84,hi=116;for(let x=0;x<128;x++){const dx=2*x-127,d2=dx*dx+dy2;let v=d2<=F?f.base:0;
  if(d2>=lo*lo&&d2<=hi*hi){const d=sector(dx,dy);for(let k=d;k<=d+1;k++){const m=k%12,ex=dx-f.bx[m],ey=dy-f.by[m];if(ex*ex+ey*ey<=256){v=m<f.lit?f.ink:f.dim;break}}}row[x]=v}}
 else if(e===10){const ey=dy-f.cy,rx2=f.rx*f.rx,ry2=f.ry*f.ry,lim=rx2*ry2,et=ey*ey*rx2,fl=dy>=97&&dy<=99;
  for(let x=0;x<128;x++){const dx=2*x-127,d2=dx*dx+dy2;row[x]=d2>F?0:fl||dx*dx*ry2+et<=lim?f.ink:f.base}}
 else if(e===11){for(let x=0;x<128;x++){const dx=2*x-127;row[x]=dx*dx+dy2<=F?f.base:0}
  for(const s of f.sp){const oy=y-s.y,ay=Math.abs(oy);if(ay>6)continue;for(let ox=-6;ox<=6;ox++){const ax=Math.abs(ox),x=s.x+ox,axis=ax===0||ay===0,r=ax+ay;if(x<0||x>=128)continue;
   const lv=s.st===0||s.st===3?(axis&&r<=1?1:0):s.st===1?(r===0?2:axis&&r<=2?1:0):s.star?(ax<=1&&ay<=1?2:axis?(r<=4?2:r<=6?1:0):0):(ax*ax+ay*ay<=5?2:ax*ax+ay*ay<=8?1:0);
   if(!lv)continue;const dx=2*x-127;if(dx*dx+dy2<=F)row[x]=lv===2?s.col:s.dim}}}
 else if(e===12||e===13){const ro=f.row[y>>1];for(let x=0;x<128;x++){const dx=2*x-127;row[x]=dx*dx+dy2>F?0:ro||f.col[x>>1]?f.ink:f.base}}}
function roll(fi,i,l){const key=fi+':'+i;if(!rolls[key]){const c=l.c===RND?rnd(0):l.c;rolls[key]={c,f:l.f===RND?rnd(c):l.f,q:(l.q||DEFQ).map(v=>v===RND?rnd(c):v)}}return rolls[key]}   /* fi = playlist position, so a looped frame re-rolls each pass like the sign does */
function query(e){const L=e.L;return L.map((l,i)=>{const r=roll(e.fi,i,l);const text=isText(l)?l.t.trim():'';const g=(isText(l)||l.k==='off')?'':l.k;return`t${i}=${encodeURIComponent(text)}&s${i}=${l.s}&c${i}=${r.c}&f${i}=${r.f}&g${i}=${g}&k${i}=${l.k==='off'?'off':''}&q${i}=${r.q.join(',')}`}).join('&')}
const preview=debounce(()=>{const gen=++animGen;clearTimeout(animTimer);frames={};fails={};rolls={};if(!$('live').checked)return;liveSend();
 const tl=timeline(),cycle=tl.reduce((s,e)=>s+e.ms,0),t0=Date.now();
 const entryAt=ms=>{const t=ms%cycle;for(const e of tl)if(t<e.start+e.ms)return[e,t];return[tl[0],t]};
 const beatFx=e=>e.L.some(l=>l.k.startsWith('fx:')||l.hc||l.hf);
 const used=e=>e.L.map(l=>SPRITES.find(s=>s.id===l.k)).filter(Boolean);
 let busy=false;
 const tick=async()=>{if(gen!==animGen)return;const ms=Date.now()-t0,[e,t]=entryAt(ms),sp=used(e),bt=beatOf(+ed.bpm||0,t-e.b0,t-e.start,e.ms);const key=e.fi+':'+e.m+':'+sp.map(s=>s.frames>1?Math.floor(ms/s.ms)%s.frames:0).join(',');
  const hasAnim=e.L.some(l=>l.k.startsWith('u:'));
  if(frames[key]){ctx.putImageData(frames[key],0,0);paintBeat(e,bt);if(hasAnim)paintAnims(e,ms,bt)}
  else if(!busy&&!(fails[key]>3)){busy=true;const b=await fetch(`/api/preview.rle?${query(e)}&m=${e.m}&k=${ms}`,{cache:'no-store'}).then(r=>r.ok?r.arrayBuffer():null).catch(()=>null);busy=false;if(gen!==animGen)return;
   const img=b&&decodeRle(b);if(img){frames[key]=img;ctx.putImageData(img,0,0);paintBeat(e,bt);if(hasAnim)paintAnims(e,ms,bt)}else{fails[key]=(fails[key]||0)+1;if(fails[key]>3)setTimeout(()=>{fails[key]=0},3000)}}
  const period=hasAnim||beatFx(e)?40:(tl.length>1||sp.length||tl.some(beatFx))?Math.min(100,...sp.map(s=>s.ms)):0;if(period)animTimer=setTimeout(tick,period)};tick()},150);

/* ---------- live on the LEDs ---------- */
let liveBusy=false,livePending=null,liveLast='';
async function livePush(){if(liveBusy)return;const body=livePending;livePending=null;if(body===null||body===liveLast)return;liveBusy=true;liveLast=body;
 try{const s=await fetch('/api/live',{method:'POST',headers:{'Content-Type':'application/json'},body}).then(r=>r.json());$('livemsg').textContent=s.live?'showing your draft on the LEDs':''}
 catch(e){$('livemsg').textContent='no connection';liveLast=''}
 liveBusy=false;if(livePending!==null)livePush()}
const isBlank=()=>cur().every(l=>(l.k==='short'||l.k==='long')&&!l.t.trim());
async function liveRelease(){livePending=null;liveLast='';try{await fetch('/api/live',{method:'DELETE'})}catch(e){}$('livemsg').textContent=''}
const liveSend=debounce(()=>{if(!$('live').checked)return;if(isBlank()){if(liveLast)liveRelease();return}livePending=JSON.stringify(draftJson());livePush()},120);
$('live').checked=(()=>{try{return localStorage.getItem('stoplight.live')==='1'}catch(e){return false}})();
function previewPane(){const on=$('live').checked;$('pvbody').classList.toggle('hide',!on);$('pvcard').classList.toggle('off',!on)}
$('live').onchange=async()=>{const on=$('live').checked;try{localStorage.setItem('stoplight.live',on?'1':'0')}catch(e){}previewPane();
 if(on){liveLast='';preview()}else{animGen++;clearTimeout(animTimer);liveRelease()}};
$('playAll').onchange=()=>preview();previewPane();

/* ---------- add / edit / list ---------- */
async function submit(asNew){if(!(ed.name||'').trim()){const n=prompt('Name for this set · שם לסט:');if(n===null)return;ed.name=n.trim();$('name').value=ed.name;persist();if(!ed.name)return}
 const editing=asNew?null:ed.editing;$('add').disabled=true;$('addNew').disabled=true;
 const r=await fetch(editing===null?'/api/sets':'/api/sets/'+editing,{method:editing===null?'POST':'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify(setJson())}).then(r=>r.json()).catch(()=>({error:'no connection'}));
 if(r.error){$('msg').textContent='Refused: '+r.error;update();return}$('msg').textContent=editing===null?'Added.':'Saved.';liveLast='';livePending=null;resetEditor();render(r)}
$('add').onclick=()=>submit(false);$('addNew').onclick=()=>submit(true);
function resetEditor(){ed={name:'',bpm:120,frames:[newFrame()],cur:0,loops:[],editing:null};persist();buildLights();refit()}
$('clear').onclick=resetEditor;$('cancel').onclick=resetEditor;
function loadSet(i){const s=state.sets[i];if(!s)return;const fromJ=(o,pos)=>({k:(o.g&&o.g[pos])?o.g[pos]:((o.k&&o.k[pos])||'short'),t:(o.t&&o.t[pos])||'',s:(o.s&&o.s[pos])||0,c:(o.c&&o.c[pos]!==undefined)?o.c[pos]:DEFC[pos],f:(o.f&&o.f[pos])||0,gm:(o.gm&&o.gm[pos])||0,af:(o.af&&o.af[pos])||0,al:(o.al&&o.al[pos])||0,hc:(o.hc&&o.hc[pos])||0,hf:(o.hf&&o.hf[pos])||0,q:(o.q&&o.q[pos])||DEFQ.slice()});
 ed={name:s.name||'',bpm:s.bpm||120,frames:s.frames.map(f=>({lights:[0,1,2].map(p=>fromJ(f,p)),ms:f.ms||1000,mp3:f.mp3||0,fade:f.fade||3})),cur:0,loops:(s.loops||[]).map(L=>({from:L.from,to:L.to,count:L.count})),editing:i};
 persist();buildLights();refit();window.scrollTo({top:0,behavior:'smooth'})}
let lastState='';
async function load(){const s=await fetch('/api/state').then(r=>r.json()).catch(()=>null);if(!s)return;const key=JSON.stringify([s.sets,s.current,s.live,s.anims]);if(key===lastState)return;lastState=key;render(s)}
function render(s){state=s;$('livemsg').textContent=s.live?'showing your draft on the LEDs':'';
 if(s.mp3){MP3=s.mp3;$('mp3msg').textContent=MP3.present?`${MP3.tracks} track${MP3.tracks===1?'':'s'} on the card (001.mp3 … ${String(MP3.tracks).padStart(3,'0')}.mp3, in the order they were copied)`+(MP3.error?' · '+MP3.error:''):'No MP3 module answering (check wiring, card, power)';
  if(document.activeElement!==$('vol')){$('vol').value=MP3.volume;$('volv').textContent=MP3.volume}if($('frameMp3'))mp3Options()}
 if(s.settings){$('offStyle').value=s.settings.offDark?'dark':'ring';$('rot').value=s.settings.rotate180?'1':'0';if('colourRotated' in s.settings)$('colrot').value=s.settings.colourRotated?'1':'0'}
 let first=!SIZES.length;if(s.sizes)SIZES=s.sizes;if(s.sprites)SPRITES=s.sprites;
 if(s.anims){s.anims.forEach(a=>{const o=ANIMS.find(x=>x.id===a.id);if(o&&o.bytes!==a.bytes)delete ANIMDATA[a.id]});const changed=JSON.stringify(s.anims)!==JSON.stringify(ANIMS);ANIMS=s.anims;animList(s.animFree,s.animTotal);if(changed&&!first){buildLights();refit()}}
 if(first){buildLights();refit()}
 const ul=$('list');ul.innerHTML='';
 if(!s.sets.length){ul.innerHTML='<li><span class="n"></span><span style="color:var(--muted)">No sets yet. The circles show without text.</span><span></span></li>'}
 s.sets.forEach((set,i)=>{const li=document.createElement('li');if(i===s.current)li.className='current';
  const src=set.frames[0]||{};const cols=src.c||DEFC;
  li.innerHTML=`<span class="n">${i+1}</span><span class="t"><strong class="nm"></strong><span style="--c:${css(cols[0])||'#888'}" dir="auto"></span><span style="--c:${css(cols[1])||'#888'}" dir="auto"></span><span style="--c:${css(cols[2])||'#888'}" dir="auto"></span><em></em></span><span class="acts"><button class="small" data-edit="${i}">Edit</button><button class="small" data-show="${i}">Show now</button><button class="small" data-del="${i}">Delete</button></span>`;
  li.querySelector('.nm').textContent=set.name||'';li.querySelector('.nm').classList.toggle('hide',!set.name);
  li.querySelectorAll('.t span').forEach((el,k)=>{const g=(src.g||[])[k];const off=(src.k||[])[k]==='off';el.textContent=off?'[off]':g?'['+((SPRITES.find(x=>x.id===g)||{}).name||(FX.find(x=>x[0]===g)||[0,''])[1].split(' ·')[0]||g)+']':((src.t||[])[k]||'');const sz=(src.s||[])[k];if(sz){const m=document.createElement('small');m.textContent='max '+sz;el.appendChild(m)}});
  const em=li.querySelector('em'),pl=playlist(set.frames,set.loops||[]),total=pl.reduce((a,fi)=>a+((set.frames[fi]||{}).ms||1000),0),snd=set.frames.filter(f=>f.mp3>0).length,fades=set.frames.filter(f=>f.mp3===-2).length,beat=set.frames.some(f=>(f.g||[]).some(g=>g&&g.startsWith('fx:'))||(f.hc||[]).some(Boolean)||(f.hf||[]).some(Boolean));em.textContent=`${set.frames.length} frame${set.frames.length>1?'s':''}${(set.loops||[]).length?', '+set.loops.map(L=>`${L.from}–${L.to} ×${L.count}`).join(', '):''} · ${(total/1000).toFixed(1)} s per play${snd?' · '+snd+' with sound':''}${fades?' · '+fades+' fade'+(fades>1?'s':''):''}${beat?' · '+(set.bpm||120)+' BPM':''}`;
  ul.appendChild(li)});
 ul.querySelectorAll('[data-edit]').forEach(b=>b.onclick=()=>loadSet(+b.dataset.edit));
 ul.querySelectorAll('[data-show]').forEach(b=>b.onclick=async()=>render(await fetch('/api/show/'+b.dataset.show,{method:'POST'}).then(r=>r.json())));
 ul.querySelectorAll('[data-del]').forEach(b=>b.onclick=async()=>{if(!confirm('Delete set '+(+b.dataset.del+1)+'?'))return;render(await fetch('/api/sets/'+b.dataset.del,{method:'DELETE'}).then(r=>r.json()))})}
load();setInterval(load,2000);if($('live').checked)liveSend();
</script></body></html>)HTML";
