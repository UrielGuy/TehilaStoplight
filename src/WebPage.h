#pragma once
#include <pgmspace.h>

// Self-contained page (the AP has no internet, so no external assets).
static const char WEB_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>StopLight</title>
<style>
:root{--bg:#14171b;--panel:#1e2329;--ink:#e8ecf0;--muted:#98a4b0;--rule:#333b45;--red:#ff2814;--orange:#ff7800;--green:#1edc3c;--bad:#ff5c5c}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,Arial,sans-serif;padding:16px}
h1{font-size:22px;margin:0 0 4px}.sub{color:var(--muted);font-size:14px;margin:0 0 16px}
.wrap{max-width:900px;margin:0 auto;display:grid;grid-template-columns:1fr 1fr;gap:16px}
@media(max-width:720px){.wrap{grid-template-columns:1fr}}
.card{background:var(--panel);border:1px solid var(--rule);border-radius:8px;padding:16px}
h2{font-size:13px;letter-spacing:.08em;text-transform:uppercase;color:var(--muted);margin:0 0 12px}
label{display:block;font-size:14px;margin:10px 0 4px}
.swatch{display:inline-block;width:18px;height:18px;border-radius:50%;margin-inline-end:8px;vertical-align:-4px;border:2px solid var(--rule);background:var(--c);cursor:pointer;padding:0}.swatch:hover{border-color:#ffb300}.swatch.ink{width:14px;height:14px;vertical-align:-2px;margin-inline-start:-4px}
.pal{position:absolute;z-index:5;background:var(--panel);border:1px solid var(--rule);border-radius:8px;padding:10px;display:grid;grid-template-columns:repeat(8,26px);gap:5px;box-shadow:0 8px 30px rgba(0,0,0,.4)}.pal button{width:26px;height:26px;border-radius:5px;border:2px solid transparent;cursor:pointer;padding:0}.pal button.on{border-color:#ffb300}.pal .named{grid-column:1/-1;display:flex;gap:5px;flex-wrap:wrap;padding-bottom:6px;border-bottom:1px solid var(--rule);margin-bottom:4px}.pal .named button{width:auto;padding:0 8px;font-size:12px;color:#fff;text-shadow:0 0 2px #000}
textarea{width:100%;font:inherit;font-size:18px;line-height:1.3;padding:10px 12px;border:1px solid var(--rule);border-radius:6px;background:#0f1215;color:var(--ink);resize:vertical}
textarea:focus{outline:2px solid #ffb300;outline-offset:1px}
.fitrow{display:flex;justify-content:space-between;align-items:center;gap:8px;margin-top:4px}.fit{font-size:13px;min-height:18px;color:var(--muted)}.sz{display:flex;align-items:center;gap:6px;font-size:13px;color:var(--muted)}.sz span{min-width:6.5em;text-align:center;font-variant-numeric:tabular-nums}.fit.bad{color:var(--bad)}.fit.ok{color:var(--green)}
button{font:inherit;padding:9px 14px;border-radius:6px;border:1px solid var(--rule);background:#2a323b;color:var(--ink);cursor:pointer}
button.primary{background:#ffb300;color:#14171b;border-color:#ffb300;font-weight:600}button:disabled{opacity:.45;cursor:not-allowed}
button.small{padding:5px 10px;font-size:13px}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-top:14px}
ul{list-style:none;margin:0;padding:0}li{display:grid;grid-template-columns:auto 1fr auto;gap:10px;align-items:center;padding:10px 0;border-bottom:1px solid var(--rule)}
li .n{color:var(--muted);font-size:13px;width:2ch}li .t{display:grid;gap:2px}li .t span{display:block;font-size:15px;white-space:pre-line}li .t span::before{content:"";display:inline-block;width:8px;height:8px;border-radius:50%;margin-inline-end:8px;background:var(--c)}li .t small{color:var(--muted);font-size:11px;margin-inline-start:8px}
li.current{background:linear-gradient(90deg,rgba(255,179,0,.12),transparent)}li .acts{display:flex;gap:6px}
.preview{display:grid;justify-items:center;align-content:start}.preview canvas{width:128px;height:384px;image-rendering:pixelated;border:1px solid var(--rule);background:#000}
.preview .live{display:flex;align-items:center;gap:8px;margin-top:10px;font-size:14px;cursor:pointer}.preview .live input{accent-color:#ffb300;width:18px;height:18px}
@media(max-width:720px){.wrap{grid-template-columns:1fr}.preview{order:-1;position:sticky;top:0;z-index:4;background:var(--panel);grid-template-columns:auto 1fr;gap:12px;align-items:center;padding:10px 12px}.preview h2{display:none}.preview canvas{width:64px;height:192px}.preview .live{margin:0}}
.interval{display:flex;gap:8px;align-items:center;font-size:14px}.interval input{width:5em;font:inherit;padding:6px 8px;border:1px solid var(--rule);border-radius:6px;background:#0f1215;color:var(--ink)}
.msg{font-size:13px;color:var(--muted);margin-top:8px;min-height:18px}
.animrow{display:flex;gap:10px;align-items:center;flex-wrap:wrap}.animrow select{font:inherit;font-size:15px;padding:8px 10px;border:1px solid var(--rule);border-radius:6px;background:#0f1215;color:var(--ink);flex:1;min-width:200px}
li .t em{display:block;font-style:normal;color:var(--muted);font-size:12px}
</style></head><body>
<h1>StopLight</h1>
<p class="sub">Add sets of three texts. The display rotates through the sets. Enter starts a new line; other breaks are chosen automatically. Texts that do not fit inside their circle are refused. &middot; אפשר לכתוב בעברית או באנגלית</p>
<div class="wrap">
  <div class="card">
    <h2>New set &middot; סט חדש</h2>
    <label for="t0"><button type="button" class="swatch" id="sw0" title="Circle colour"></button><button type="button" class="swatch ink" id="fw0" title="Text / figure colour"></button>Top &middot; עליון</label><div class="animrow"><select id="g0" aria-label="Graphic"><option value="">Text</option></select></div><textarea id="t0" dir="auto" maxlength="240" rows="6" autocomplete="off"></textarea><div class="fitrow"><div class="fit" id="f0"></div><div class="sz"><button class="small" id="dn0" title="Smaller glyphs" disabled>A&minus;</button><span id="sz0"></span><button class="small" id="up0" title="Larger glyphs" disabled>A+</button></div></div>
    <label for="t1"><button type="button" class="swatch" id="sw1" title="Circle colour"></button><button type="button" class="swatch ink" id="fw1" title="Text / figure colour"></button>Middle &middot; אמצעי</label><div class="animrow"><select id="g1" aria-label="Graphic"><option value="">Text</option></select></div><textarea id="t1" dir="auto" maxlength="240" rows="6" autocomplete="off"></textarea><div class="fitrow"><div class="fit" id="f1"></div><div class="sz"><button class="small" id="dn1" title="Smaller glyphs" disabled>A&minus;</button><span id="sz1"></span><button class="small" id="up1" title="Larger glyphs" disabled>A+</button></div></div>
    <label for="t2"><button type="button" class="swatch" id="sw2" title="Circle colour"></button><button type="button" class="swatch ink" id="fw2" title="Text / figure colour"></button>Bottom &middot; תחתון</label><div class="animrow"><select id="g2" aria-label="Graphic"><option value="">Text</option></select></div><textarea id="t2" dir="auto" maxlength="240" rows="6" autocomplete="off"></textarea><div class="fitrow"><div class="fit" id="f2"></div><div class="sz"><button class="small" id="dn2" title="Smaller glyphs" disabled>A&minus;</button><span id="sz2"></span><button class="small" id="up2" title="Larger glyphs" disabled>A+</button></div></div>
    <label for="anim">Animation &middot; אנימציה</label>
    <div class="animrow"><select id="anim"></select><span class="interval">unit <input type="number" id="unit" min="100" max="60000" step="100" value="1000"> ms</span></div>
    <div class="row"><button class="primary" id="add" disabled>Add set</button><button id="clear">Clear</button></div>
    <div class="msg" id="msg"></div>
  </div>
  <div class="card preview"><h2>Preview &middot; as the LEDs will show it</h2><canvas id="pv" width="128" height="384"></canvas>
    <label class="live"><input type="checkbox" id="live"> Live on the LEDs while editing</label><div class="msg" id="livemsg"></div></div>
  <div class="card" style="grid-column:1/-1">
    <h2>Sets on the display</h2>
    <ul id="list"></ul>
    <div class="row"><div class="interval">Change every <input type="number" id="interval" min="5" max="86400"> seconds <button class="small" id="saveInt">Save</button></div></div>
  </div>
</div>
<script>
const $=id=>document.getElementById(id);const T=[0,1,2].map(i=>$('t'+i)),F=[0,1,2].map(i=>$('f'+i));
const fits=[false,false,false],caps=[0,0,0],eff=[0,0,0],maxFit=[0,0,0];let SIZES=[],state={sets:[],current:0,interval:60};
function debounce(fn,ms){let h;return(...a)=>{clearTimeout(h);h=setTimeout(()=>fn(...a),ms)}}
function sizeUI(i){const ok=fits[i]&&!gfx[i],min=SIZES.length?Math.min(...SIZES):0;
 $('sz'+i).textContent=ok?`size ${eff[i]}${caps[i]?'':' (auto)'}`:'';
 $('up'+i).disabled=!ok||eff[i]>=maxFit[i];$('dn'+i).disabled=!ok||eff[i]<=min}
async function check(i){if(gfx[i]){fits[i]=true;F[i].className='fit ok';F[i].textContent='graphic: '+(SPRITES.find(s=>s.id===gfx[i])||{}).name;sizeUI(i);update();return}const v=T[i].value.trim();if(!v){F[i].textContent='';F[i].className='fit';fits[i]=false;sizeUI(i);update();return}
 const r=await fetch(`/api/check?text=${encodeURIComponent(v)}&light=${i}&size=${caps[i]}`).then(r=>r.json()).catch(()=>({ok:false,error:'no connection'}));
 if(!r.ok&&caps[i]&&r.maxFit){caps[i]=0;return check(i)}   /* cap no longer workable: back to auto */
 fits[i]=!!r.ok;eff[i]=r.size||0;maxFit[i]=r.maxFit||0;
 F[i].className='fit '+(r.ok?'ok':'bad');F[i].textContent=r.ok?`fits: ${r.lines} line${r.lines>1?'s':''}`:'does not fit: '+r.error;sizeUI(i);update()}
function step(i,dir){if(!fits[i])return;const s=dir>0?SIZES.filter(x=>x>eff[i]).sort((a,b)=>a-b)[0]:SIZES.filter(x=>x<eff[i]).sort((a,b)=>b-a)[0];
 if(s===undefined)return;caps[i]=(dir>0&&s>=maxFit[i])?0:s;check(i);preview()}
const DEFC=[48,52,12],cols=DEFC.slice(),inks=[0,0,0],gfx=['','',''],LV=[0,110,185,255];let SPRITES=[];
const css=c=>`rgb(${LV[(c>>4)&3]},${LV[(c>>2)&3]},${LV[c&3]})`;
const NAMED=[['Red',48],['Orange',52],['Yellow',60],['Green',12],['Cyan',15],['Blue',3],['Magenta',51],['White',63],['Pink',53],['Lime',28]];
function swatchUI(i){$('sw'+i).style.setProperty('--c',css(cols[i]));$('fw'+i).style.setProperty('--c',css(inks[i]))}
function openPal(i,which){document.querySelectorAll('.pal').forEach(p=>p.remove());const pal=document.createElement('div');pal.className='pal';
 const named=document.createElement('div');named.className='named';const cur=which==='ink'?inks[i]:cols[i];
 (which==='ink'?[['Black',0],...NAMED]:NAMED).forEach(([n,c])=>{const b=document.createElement('button');b.type='button';b.textContent=n;b.style.background=css(c);b.onclick=()=>pick(i,c,which);named.appendChild(b)});pal.appendChild(named);
 for(let c=(which==='ink'?0:1);c<64;c++){const b=document.createElement('button');b.type='button';b.style.background=css(c);b.title='rgb '+((c>>4)&3)+((c>>2)&3)+(c&3);if(c===cur)b.className='on';b.onclick=()=>pick(i,c,which);pal.appendChild(b)}
 const sw=$((which==='ink'?'fw':'sw')+i),r=sw.getBoundingClientRect();pal.style.left=(r.left+window.scrollX)+'px';pal.style.top=(r.bottom+window.scrollY+6)+'px';document.body.appendChild(pal);
 setTimeout(()=>document.addEventListener('click',function h(e){if(!pal.contains(e.target)){pal.remove();document.removeEventListener('click',h)}}),0)}
function pick(i,c,which){if(which==='ink')inks[i]=c;else cols[i]=c;swatchUI(i);document.querySelectorAll('.pal').forEach(p=>p.remove());preview()}
[0,1,2].forEach(i=>{swatchUI(i);$('sw'+i).onclick=e=>{e.preventDefault();e.stopPropagation();openPal(i,'bg')};$('fw'+i).onclick=e=>{e.preventDefault();e.stopPropagation();openPal(i,'ink')};
 $('g'+i).onchange=()=>{gfx[i]=$('g'+i).value;T[i].disabled=!!gfx[i];T[i].style.opacity=gfx[i]?.4:1;check(i);preview()}});
let ANIMS=[],frames={},animTimer=null,animGen=0;
function curAnim(){return ANIMS.find(a=>a.id===$('anim').value)||ANIMS[0]||{steps:[[7,1]]}}
const ctx=$('pv').getContext('2d');
function decodeRle(buf){const b=new Uint8Array(buf);if(b[0]!==82||b[1]!==76)return null;const w=b[2]|(b[3]<<8),h=b[4]|(b[5]<<8),n=b[6];const pal=[];for(let i=0;i<n;i++)pal.push([b[7+i*3],b[8+i*3],b[9+i*3]]);
 const img=ctx.createImageData(w,h),d=img.data;let p=0,o=7+n*3;while(o+1<b.length){const v=b[o]|(b[o+1]<<8);o+=2;const c=pal[v>>12]||[0,0,0];let len=v&4095;while(len--&&p<d.length){d[p++]=c[0];d[p++]=c[1];d[p++]=c[2];d[p++]=255}}return img}
function show(img){if(img)ctx.putImageData(img,0,0)}
let liveBusy=false,livePending=null,liveLast='';
async function livePush(){if(liveBusy)return;const body=livePending;livePending=null;if(body===null||body===liveLast)return;liveBusy=true;liveLast=body;
 try{const s=await fetch('/api/live',{method:'POST',headers:{'Content-Type':'application/json'},body}).then(r=>r.json());$('livemsg').textContent=s.live?'showing your draft on the LEDs':''}
 catch(e){$('livemsg').textContent='no connection';liveLast=''}
 liveBusy=false;if(livePending!==null)livePush()}
const liveSend=debounce(()=>{if(!$('live').checked)return;livePending=JSON.stringify(draft());livePush()},120);
function draft(){return{t:T.map(t=>t.value.trim()),s:caps.slice(),c:cols.slice(),f:inks.slice(),g:gfx.slice(),a:$('anim').value,p:+$('unit').value||1000}}
const preview=debounce(()=>{const gen=++animGen;clearTimeout(animTimer);frames={};liveSend();
 const q=T.map((t,i)=>`t${i}=${encodeURIComponent(t.value.trim())}&s${i}=${caps[i]}&c${i}=${cols[i]}&f${i}=${inks[i]}&g${i}=${gfx[i]}`).join('&');
 const a=curAnim(),unit=()=>Math.max(100,+$('unit').value||1000);
 const used=gfx.map(id=>SPRITES.find(s=>s.id===id)).filter(Boolean);
 const t0=Date.now();
 const maskAt=ms=>{if(a.steps.length<2)return a.steps[0][0];const cyc=a.steps.reduce((s,x)=>s+x[1],0)*unit();let t=ms%cyc;for(const st of a.steps){const d=st[1]*unit();if(t<d)return st[0];t-=d}return a.steps[0][0]};
 const keyAt=ms=>maskAt(ms)+':'+used.map(s=>s.frames>1?Math.floor(ms/s.ms)%s.frames:0).join(',');
 let busy=false;
 const tick=async()=>{if(gen!==animGen)return;const ms=Date.now()-t0,key=keyAt(ms);
  if(frames[key])show(frames[key]);
  else if(!busy){busy=true;const b=await fetch(`/api/preview.rle?${q}&m=${maskAt(ms)}&k=${ms}`).then(r=>r.arrayBuffer()).catch(()=>null);busy=false;if(gen!==animGen)return;if(b){frames[key]=decodeRle(b);show(frames[key])}}
  const period=used.length?Math.min(100,...used.map(s=>s.ms)):(a.steps.length>1?100:0);
  if(period)animTimer=setTimeout(tick,period)};tick()},150);
$('anim').addEventListener('change',preview);$('unit').addEventListener('change',preview);
function update(){$('add').disabled=!(fits[0]&&fits[1]&&fits[2])}
T.forEach((t,i)=>{const c=debounce(()=>check(i),250);t.addEventListener('input',()=>{c();preview()});$('up'+i).onclick=()=>step(i,1);$('dn'+i).onclick=()=>step(i,-1)});
$('clear').onclick=()=>{T.forEach(t=>{t.value='';t.disabled=false;t.style.opacity=1});F.forEach(f=>f.textContent='');fits.fill(false);caps.fill(0);gfx.fill('');[0,1,2].forEach(i=>$('g'+i).value='');DEFC.forEach((c,i)=>{cols[i]=c;inks[i]=0;swatchUI(i)});[0,1,2].forEach(sizeUI);update();preview()};
$('add').onclick=async()=>{$('add').disabled=true;const body={t:T.map(t=>t.value.trim()),s:caps.slice(),c:cols.slice(),f:inks.slice(),g:gfx.slice(),a:$('anim').value,p:+$('unit').value||1000};
 const r=await fetch('/api/sets',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}).then(r=>r.json());
 if(r.error){$('msg').textContent='Refused: '+r.error;update();return}$('msg').textContent='Added.';$('clear').click();render(r)};
let lastState='';
async function load(){const s=await fetch('/api/state').then(r=>r.json()).catch(()=>null);if(!s)return;const key=JSON.stringify([s.sets,s.current,s.interval,s.live]);if(key===lastState)return;lastState=key;render(s)}
$('live').checked=(()=>{try{return localStorage.getItem('stoplight.live')==='1'}catch(e){return false}})();
$('live').onchange=async()=>{const on=$('live').checked;try{localStorage.setItem('stoplight.live',on?'1':'0')}catch(e){}
 if(on){liveLast='';liveSend()}else{livePending=null;liveLast='';await fetch('/api/live',{method:'DELETE'}).catch(()=>{});$('livemsg').textContent=''}};
function render(s){state=s;$('livemsg').textContent=s.live?'showing your draft on the LEDs':'';if(s.sizes)SIZES=s.sizes;if(s.sprites&&!SPRITES.length){SPRITES=s.sprites;[0,1,2].forEach(i=>{const sel=$('g'+i);SPRITES.forEach(sp=>{const o=document.createElement('option');o.value=sp.id;o.textContent=sp.name;sel.appendChild(o)})})}
 if(s.animations&&!ANIMS.length){ANIMS=s.animations;const sel=$('anim');ANIMS.forEach(a=>{const o=document.createElement('option');o.value=a.id;o.textContent=a.name;sel.appendChild(o)});preview()}
 $('interval').value=s.interval;const ul=$('list');ul.innerHTML='';
 if(!s.sets.length){ul.innerHTML='<li><span class="n"></span><span style="color:var(--muted)">No sets yet. The circles show without text.</span><span></span></li>'}
 s.sets.forEach((set,i)=>{const li=document.createElement('li');if(i===s.current)li.className='current';
  const sc=set.c||DEFC;li.innerHTML=`<span class="n">${i+1}</span><span class="t"><span style="--c:${css(sc[0])}" dir="auto"></span><span style="--c:${css(sc[1])}" dir="auto"></span><span style="--c:${css(sc[2])}" dir="auto"></span></span><span class="acts"><button class="small" data-show="${i}">Show now</button><button class="small" data-del="${i}">Delete</button></span>`;
  li.querySelectorAll('.t span').forEach((el,k)=>{const g=(set.g||[])[k];el.textContent=g?'['+((SPRITES.find(x=>x.id===g)||{}).name||g)+']':set.t[k];const sz=(set.s||[])[k];if(sz){const m=document.createElement('small');m.textContent='max '+sz;el.appendChild(m)}});
  const an=ANIMS.find(a=>a.id===set.a);if(an&&an.steps.length>1){const e=document.createElement('em');e.textContent=an.name+' · '+(set.p||1000)+' ms';li.querySelector('.t').appendChild(e)}ul.appendChild(li)});
 ul.querySelectorAll('[data-show]').forEach(b=>b.onclick=async()=>render(await fetch('/api/show/'+b.dataset.show,{method:'POST'}).then(r=>r.json())));
 ul.querySelectorAll('[data-del]').forEach(b=>b.onclick=async()=>{if(!confirm('Delete set '+(+b.dataset.del+1)+'?'))return;render(await fetch('/api/sets/'+b.dataset.del,{method:'DELETE'}).then(r=>r.json()))})}
$('saveInt').onclick=async()=>render(await fetch('/api/interval',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({seconds:+$('interval').value})}).then(r=>r.json()));
load();setInterval(load,2000);if($('live').checked)liveSend();
</script></body></html>)HTML";
