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
.swatch.rnd{background:conic-gradient(#f00,#ff0,#0f0,#0ff,#00f,#f0f,#f00)}.lhead .off{padding:3px 8px;font-size:12px}.lhead .off.on{background:var(--acc);color:#14171b;border-color:var(--acc)}
.fitrow{display:flex;justify-content:space-between;align-items:center;gap:8px;margin-top:4px;flex-wrap:wrap}.fit{font-size:13px;min-height:18px;color:var(--muted)}.fit.bad{color:var(--bad)}.fit.ok{color:var(--green)}
.sz{display:flex;align-items:center;gap:6px;font-size:13px;color:var(--muted)}.sz span{min-width:6.5em;text-align:center;font-variant-numeric:tabular-nums}
.tm{display:flex;align-items:center;gap:6px;font-size:13px;color:var(--muted)}.tm input[type=checkbox]{accent-color:var(--acc);width:18px;height:18px}
button{font:inherit;padding:9px 14px;border-radius:6px;border:1px solid var(--rule);background:#2a323b;color:var(--ink);cursor:pointer}
button.primary{background:var(--acc);color:#14171b;border-color:var(--acc);font-weight:600}button:disabled{opacity:.45;cursor:not-allowed}
button.small{padding:5px 10px;font-size:13px}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-top:12px}
.frames{display:flex;gap:6px;flex-wrap:wrap;align-items:center;margin:8px 0}.frames button.cur{border-color:var(--acc);color:var(--acc)}
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
  <div class="card" id="editor">
    <h2 id="edTitle">New set &middot; סט חדש</h2>
    <input type="text" id="name" maxlength="40" placeholder="Name this set (optional) · שם" autocomplete="off" style="font-size:16px;margin-bottom:10px">
    <div id="framesBar">
      <div class="frames" id="frames"></div>
      <div class="row" style="margin-top:4px"><span class="tm">Show this frame for <input type="number" id="frameMs" min="100" max="600000" step="100"> ms</span></div>
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
</div>
<script>
const $=id=>document.getElementById(id);
const DEFC=[48,52,12],RND=64,LV=[0,140,195,255],NAMES=['Top · עליון','Middle · אמצעי','Bottom · תחתון'];
const NAMED=[['Red',48],['Orange',52],['Yellow',60],['Green',12],['Cyan',15],['Blue',3],['Magenta',51],['White',63],['Pink',53],['Lime',28]];
let SIZES=[],SPRITES=[],state={sets:[],current:0};
const css=c=>c===RND?'':`rgb(${LV[(c>>4)&3]},${LV[(c>>2)&3]},${LV[c&3]})`;
const recent=[0,0];const rnd=avoid=>{let c;for(let t=0;t<32;t++){c=1+Math.floor(Math.random()*63);if(c!==avoid&&c!==recent[0]&&c!==recent[1])break}recent[1]=recent[0];recent[0]=c;return c};
const newLight=i=>({k:'short',t:'',s:0,c:DEFC[i],f:0});
const newFrame=()=>({lights:[0,1,2].map(newLight),ms:1000});
const MAXF=64;
let ed={name:'',frames:[newFrame()],cur:0,loops:[],editing:null};
try{const s=JSON.parse(localStorage.getItem('stoplight.editor')||'null');if(s&&s.frames&&s.frames.length)ed={...ed,name:s.name||'',frames:s.frames,cur:Math.min(s.cur||0,s.frames.length-1),loops:s.loops||[]}}catch(e){}
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
  sel.value=l.k;$('t'+i).value=l.k==='short'?l.t:'';$('l'+i).value=l.k==='long'?l.t:'';
  const setKind=k=>{const prev=l.k;l.k=k;sel.value=k;if(l.k==='short'&&prev==='long')l.t=l.t.replace(/\s*\n\s*/g,' ');if(l.k==='short')$('t'+i).value=l.t;if(l.k==='long')$('l'+i).value=l.t;showBoxes(i);changed(i)};
  sel.onchange=()=>setKind(sel.value);$('off'+i).onclick=()=>setKind(l.k==='off'?'short':'off');
  const c=debounce(()=>changed(i),200);
  $('t'+i).addEventListener('input',()=>{l.t=$('t'+i).value;c()});$('l'+i).addEventListener('input',()=>{l.t=$('l'+i).value;c()});
  $('sw'+i).onclick=e=>{e.preventDefault();e.stopPropagation();openPal(i,'bg')};$('fw'+i).onclick=e=>{e.preventDefault();e.stopPropagation();openPal(i,'ink')};
  $('up'+i).onclick=()=>step(i,1);$('dn'+i).onclick=()=>step(i,-1);
  swatchUI(i);showBoxes(i)});
 {const fb=$('frames');fb.innerHTML='';ed.frames.forEach((f,i)=>{const b=document.createElement('button');b.type='button';b.className='small'+(i===ed.cur?' cur':'');b.textContent=(i+1);b.onclick=()=>{ed.cur=i;persist();buildLights();refit()};fb.appendChild(b)});
  const add=document.createElement('button');add.type='button';add.className='small';add.textContent='+ at end';add.disabled=ed.frames.length>=MAXF;add.onclick=()=>insertFrame(ed.frames.length);fb.appendChild(add);
  $('frameMs').value=ed.frames[ed.cur].ms;buildLoops()}
 $('name').value=ed.name||'';$('add').textContent=ed.editing===null?'Add set':'Save changes';$('addNew').classList.toggle('hide',ed.editing===null);$('cancel').classList.toggle('hide',ed.editing===null);$('edTitle').textContent=ed.editing===null?'New set · סט חדש':'Editing set '+(ed.editing+1)+(ed.name?' · '+ed.name:'')}
function showBoxes(i){const l=cur()[i],g=l.k!=='short'&&l.k!=='long';$('t'+i).classList.toggle('hide',l.k!=='short');$('l'+i).classList.toggle('hide',l.k!=='long');$('szrow'+i).classList.toggle('hide',g);$('off'+i).classList.toggle('on',l.k==='off')}
function swatchUI(i){const l=cur()[i];const sw=$('sw'+i),fw=$('fw'+i);sw.classList.toggle('rnd',l.c===RND);fw.classList.toggle('rnd',l.f===RND);sw.style.setProperty('--c',css(l.c));fw.style.setProperty('--c',css(l.f))}
function openPal(i,which){document.querySelectorAll('.pal').forEach(p=>p.remove());const l=cur()[i],pal=document.createElement('div');pal.className='pal';const v=which==='ink'?l.f:l.c;
 const named=document.createElement('div');named.className='named';
 const mk=(n,c,bg)=>{const b=document.createElement('button');b.type='button';b.textContent=n;b.style.background=bg;b.onclick=()=>pick(i,c,which);if(c===v)b.className='on';named.appendChild(b)};
 mk('Random',RND,'conic-gradient(#f00,#ff0,#0f0,#0ff,#00f,#f0f,#f00)');mk('Black',0,'#000');NAMED.forEach(([n,c])=>mk(n,c,css(c)));pal.appendChild(named);
 for(let c=0;c<64;c++){const b=document.createElement('button');b.type='button';b.style.background=css(c);b.title='rgb '+((c>>4)&3)+((c>>2)&3)+(c&3);if(c===v)b.className='on';b.onclick=()=>pick(i,c,which);pal.appendChild(b)}
 const sw=$((which==='ink'?'fw':'sw')+i),r=sw.getBoundingClientRect();pal.style.left=Math.min(r.left+window.scrollX,window.innerWidth-260)+'px';pal.style.top=(r.bottom+window.scrollY+6)+'px';document.body.appendChild(pal);
 setTimeout(()=>document.addEventListener('click',function h(e){if(!pal.contains(e.target)){pal.remove();document.removeEventListener('click',h)}}),0)}
function pick(i,c,which){const l=cur()[i];if(which==='ink')l.f=c;else l.c=c;swatchUI(i);document.querySelectorAll('.pal').forEach(p=>p.remove());persist();preview()}

/* ---------- fit checks and sizes ---------- */
async function checkLight(i){const l=cur()[i];if(l.k==='off'){fits[i]=true;F(i,'','light off');sizeUI(i);update();return}if(l.k!=='short'&&l.k!=='long'){fits[i]=true;F(i,'ok','graphic: '+((SPRITES.find(s=>s.id===l.k)||{}).name||l.k));sizeUI(i);update();return}
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
// Insert a blank frame at `at`; loop sections at or beyond it shift to keep pointing at the same frames.
function insertFrame(at,copy){if(ed.frames.length>=MAXF)return;ed.frames.splice(at,0,copy?JSON.parse(JSON.stringify(ed.frames[ed.cur])):newFrame());
 ed.loops.forEach(L=>{if(L.from>at)L.from++;if(L.to>=at+1&&L.to>=L.from)L.to++;if(L.from>at&&L.to<L.from)L.to=L.from});ed.cur=at;persist();buildLights();refit()}
$('insBefore').onclick=()=>insertFrame(ed.cur);$('insAfter').onclick=()=>insertFrame(ed.cur+1);
$('dupFrame').onclick=()=>insertFrame(ed.cur+1,true);
$('delFrame').onclick=()=>{if(ed.frames.length<=1)return;const at=ed.cur;ed.frames.splice(at,1);
 ed.loops=ed.loops.map(L=>({from:L.from>at+1?L.from-1:L.from,to:L.to>at?L.to-1:L.to,count:L.count})).filter(L=>L.to>=L.from);ed.cur=Math.max(0,at-1);persist();buildLights();refit()};
$('name').addEventListener('input',()=>{ed.name=$('name').value;persist()});

/* ---------- what gets sent ---------- */
const isText=l=>l.k==='short'||l.k==='long';
function lightsJson(L){return{t:L.map(l=>isText(l)?l.t.trim():''),k:L.map(l=>l.k==='long'?'long':l.k==='off'?'off':'short'),g:L.map(l=>(isText(l)||l.k==='off')?'':l.k),s:L.map(l=>l.s),c:L.map(l=>l.c),f:L.map(l=>l.f)}}
const maskOf=L=>L.reduce((m,l,i)=>m|(l.k!=='off'?1<<i:0),0);
function setJson(){const name=(ed.name||'').trim().slice(0,40);return{name,frames:ed.frames.map(f=>({...lightsJson(f.lights),m:maskOf(f.lights),ms:f.ms})),loops:ed.loops.map(L=>({from:L.from,to:L.to,count:L.count}))}}
function playlist(frames,loops){const out=[],n=frames.length;let i=0;while(i<n&&out.length<400){const L=loops.find(x=>x.from-1===i&&x.to<=n&&x.to>=x.from);if(L){for(let c=0;c<L.count&&out.length<400;c++)for(let k=i;k<L.to;k++)out.push(k);i=L.to}else{out.push(i);i++}}return out}
function draftJson(){const L=cur();return{...lightsJson(L),m:maskOf(L)}}

/* ---------- preview: timeline of {lights, mask, ms} played on a canvas ---------- */
const ctx=$('pv').getContext('2d');let frames={},fails={},animTimer=null,animGen=0,rolls={};
function decodeRle(buf){const b=new Uint8Array(buf);if(b.length<7||b[0]!==82||b[1]!==76)return null;const w=b[2]|(b[3]<<8),h=b[4]|(b[5]<<8),n=b[6];const pal=[];for(let i=0;i<n;i++)pal.push([b[7+i*3],b[8+i*3],b[9+i*3]]);
 const img=ctx.createImageData(w,h),d=img.data;let p=0,o=7+n*3;while(o+1<b.length){const v=b[o]|(b[o+1]<<8);o+=2;const c=pal[v>>12]||[0,0,0];let len=v&4095;while(len--&&p<d.length){d[p++]=c[0];d[p++]=c[1];d[p++]=c[2];d[p++]=255}}
 return p===d.length?img:null}   /* incomplete stream: reject, the caller refetches */
function timeline(){if(!$('playAll').checked){const f=ed.frames[ed.cur];return[{L:f.lights,m:maskOf(f.lights),ms:f.ms,fi:ed.cur}]}
 const pl=loopErrors().some(Boolean)?ed.frames.map((_,i)=>i):playlist(ed.frames,ed.loops);return pl.map((fi,pi)=>{const f=ed.frames[fi];return{L:f.lights,m:maskOf(f.lights),ms:f.ms,fi:pi}})}
function roll(fi,i,l){const key=fi+':'+i;if(!rolls[key]){const c=l.c===RND?rnd(0):l.c;rolls[key]={c,f:l.f===RND?rnd(c):l.f}}return rolls[key]}   /* fi = playlist position, so a looped frame re-rolls each pass like the sign does */
function query(e){const L=e.L;return L.map((l,i)=>{const r=roll(e.fi,i,l);const text=isText(l)?l.t.trim():'';const g=(isText(l)||l.k==='off')?'':l.k;return`t${i}=${encodeURIComponent(text)}&s${i}=${l.s}&c${i}=${r.c}&f${i}=${r.f}&g${i}=${g}&k${i}=${l.k==='off'?'off':''}`}).join('&')}
const preview=debounce(()=>{const gen=++animGen;clearTimeout(animTimer);frames={};fails={};rolls={};if(!$('live').checked)return;liveSend();
 const tl=timeline(),cycle=tl.reduce((s,e)=>s+e.ms,0),t0=Date.now();
 const entryAt=ms=>{let t=ms%cycle;for(const e of tl){if(t<e.ms)return e;t-=e.ms}return tl[0]};
 const used=e=>e.L.map(l=>SPRITES.find(s=>s.id===l.k)).filter(Boolean);
 let busy=false;
 const tick=async()=>{if(gen!==animGen)return;const ms=Date.now()-t0,e=entryAt(ms),sp=used(e);const key=e.fi+':'+e.m+':'+sp.map(s=>s.frames>1?Math.floor(ms/s.ms)%s.frames:0).join(',');
  if(frames[key])ctx.putImageData(frames[key],0,0);
  else if(!busy&&!(fails[key]>3)){busy=true;const b=await fetch(`/api/preview.rle?${query(e)}&m=${e.m}&k=${ms}`,{cache:'no-store'}).then(r=>r.ok?r.arrayBuffer():null).catch(()=>null);busy=false;if(gen!==animGen)return;
   const img=b&&decodeRle(b);if(img){frames[key]=img;ctx.putImageData(img,0,0)}else{fails[key]=(fails[key]||0)+1;if(fails[key]>3)setTimeout(()=>{fails[key]=0},3000)}}
  const period=(tl.length>1||sp.length)?Math.min(100,...sp.map(s=>s.ms)):0;if(period)animTimer=setTimeout(tick,period)};tick()},150);

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
function resetEditor(){ed={name:'',frames:[newFrame()],cur:0,loops:[],editing:null};persist();buildLights();refit()}
$('clear').onclick=resetEditor;$('cancel').onclick=resetEditor;
function loadSet(i){const s=state.sets[i];if(!s)return;const fromJ=(o,pos)=>({k:(o.g&&o.g[pos])?o.g[pos]:((o.k&&o.k[pos])||'short'),t:(o.t&&o.t[pos])||'',s:(o.s&&o.s[pos])||0,c:(o.c&&o.c[pos]!==undefined)?o.c[pos]:DEFC[pos],f:(o.f&&o.f[pos])||0});
 ed={name:s.name||'',frames:s.frames.map(f=>({lights:[0,1,2].map(p=>fromJ(f,p)),ms:f.ms||1000})),cur:0,loops:(s.loops||[]).map(L=>({from:L.from,to:L.to,count:L.count})),editing:i};
 persist();buildLights();refit();window.scrollTo({top:0,behavior:'smooth'})}
let lastState='';
async function load(){const s=await fetch('/api/state').then(r=>r.json()).catch(()=>null);if(!s)return;const key=JSON.stringify([s.sets,s.current,s.live]);if(key===lastState)return;lastState=key;render(s)}
function render(s){state=s;$('livemsg').textContent=s.live?'showing your draft on the LEDs':'';
 let first=!SIZES.length;if(s.sizes)SIZES=s.sizes;if(s.sprites)SPRITES=s.sprites;
 if(first){buildLights();refit()}
 const ul=$('list');ul.innerHTML='';
 if(!s.sets.length){ul.innerHTML='<li><span class="n"></span><span style="color:var(--muted)">No sets yet. The circles show without text.</span><span></span></li>'}
 s.sets.forEach((set,i)=>{const li=document.createElement('li');if(i===s.current)li.className='current';
  const src=set.frames[0]||{};const cols=src.c||DEFC;
  li.innerHTML=`<span class="n">${i+1}</span><span class="t"><strong class="nm"></strong><span style="--c:${css(cols[0])||'#888'}" dir="auto"></span><span style="--c:${css(cols[1])||'#888'}" dir="auto"></span><span style="--c:${css(cols[2])||'#888'}" dir="auto"></span><em></em></span><span class="acts"><button class="small" data-edit="${i}">Edit</button><button class="small" data-show="${i}">Show now</button><button class="small" data-del="${i}">Delete</button></span>`;
  li.querySelector('.nm').textContent=set.name||'';li.querySelector('.nm').classList.toggle('hide',!set.name);
  li.querySelectorAll('.t span').forEach((el,k)=>{const g=(src.g||[])[k];const off=(src.k||[])[k]==='off';el.textContent=off?'[off]':g?'['+((SPRITES.find(x=>x.id===g)||{}).name||g)+']':((src.t||[])[k]||'');const sz=(src.s||[])[k];if(sz){const m=document.createElement('small');m.textContent='max '+sz;el.appendChild(m)}});
  const em=li.querySelector('em'),pl=playlist(set.frames,set.loops||[]),total=pl.reduce((a,fi)=>a+((set.frames[fi]||{}).ms||1000),0);em.textContent=`${set.frames.length} frame${set.frames.length>1?'s':''}${(set.loops||[]).length?', '+set.loops.map(L=>`${L.from}–${L.to} ×${L.count}`).join(', '):''} · ${(total/1000).toFixed(1)} s per play`;
  ul.appendChild(li)});
 ul.querySelectorAll('[data-edit]').forEach(b=>b.onclick=()=>loadSet(+b.dataset.edit));
 ul.querySelectorAll('[data-show]').forEach(b=>b.onclick=async()=>render(await fetch('/api/show/'+b.dataset.show,{method:'POST'}).then(r=>r.json())));
 ul.querySelectorAll('[data-del]').forEach(b=>b.onclick=async()=>{if(!confirm('Delete set '+(+b.dataset.del+1)+'?'))return;render(await fetch('/api/sets/'+b.dataset.del,{method:'DELETE'}).then(r=>r.json()))})}
load();setInterval(load,2000);if($('live').checked)liveSend();
</script></body></html>)HTML";
