#!/usr/bin/env python3
"""Check the page's beat-animation preview (WebPage.h: hueTurn, beatOf,
pulseD, paintBeat) against the C++ that drives the LEDs (Face::drawLight
via tools/stopcli): for each case the page paints the beat over the server's
image of the frame's start, which must equal stopcli's image at that moment.

    python3 tools/beat_jstest.py        # WSL; needs Windows Node (node.exe)

Exit code 1 on any differing pixel.
"""
import json, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, ".pio", "beattest")
NODE = "/mnt/c/Program Files/nodejs/node.exe"
CLI = os.path.join(ROOT, "tools", "stopcli")

LIGHTS = [   # (page light, extra server args)
    [{"k": "short", "t": "STOP", "c": 48, "f": 0, "hc": 30}, {"k": "fx:quarters", "c": 48, "f": 0, "q": [60, 12, 3]}, {"k": "fx:pulse", "c": 12, "f": 0, "hc": 20}],
    [{"k": "short", "t": "GO", "c": 0, "f": 51, "hf": -40}, {"k": "fx:pulse", "c": 63, "f": 0}, {"k": "fx:quarters", "c": 53, "f": 0, "hc": -70, "q": [15, 0, 28]}],
    [{"k": "dancing", "c": 52, "f": 3, "hc": 50, "hf": 10}, {"k": "short", "t": "המתן", "c": 21, "f": 21, "hc": 90}, {"k": "fx:pulse", "c": 22, "f": 0, "hc": 359}],
]
TIMES = [(120, 0, 0), (120, 1, 1), (120, 499, 499), (120, 500, 500), (120, 1250, 1250), (97, 5000, 1234), (240, 61000, 300), (60, 3999, 3999), (400, 777777, 12345)]


def args_for(L, mask=7):
    a = [f"m={mask}"]
    for i, l in enumerate(L):
        if l["k"] == "short":
            a.append(f"t{i}={l['t']}")
        else:
            a.append(f"g{i}={l['k']}")
        a += [f"c{i}={l['c']}", f"f{i}={l['f']}", f"q{i}={','.join(map(str, l.get('q', [60, 12, 3])))}"]
    return a


def rle(args):
    return subprocess.run([CLI, "rle"] + args, capture_output=True, check=True).stdout


def main():
    os.makedirs(OUT, exist_ok=True)
    cases = []
    for n, L in enumerate(LIGHTS):
        for mask in (7, 5):
            for bpm, grid, frame in TIMES:
                base = args_for(L, mask) + [f"k={frame}"]
                beat = base + [f"bpm={bpm}", f"b={grid}"] + [f"hc{i}={l.get('hc', 0)}" for i, l in enumerate(L)] + [f"hf{i}={l.get('hf', 0)}" for i, l in enumerate(L)]
                stem = f"c{len(cases)}"
                open(os.path.join(OUT, stem + ".base"), "wb").write(rle(base))
                open(os.path.join(OUT, stem + ".want"), "wb").write(rle(beat))
                cases.append({"stem": stem, "L": L, "m": mask, "bpm": bpm, "grid": grid, "frame": frame})
    json.dump(cases, open(os.path.join(OUT, "cases.json"), "w"))
    src = open(os.path.join(ROOT, "src", "WebPage.h"), encoding="utf-8").read()
    open(os.path.join(OUT, "page.js"), "w", encoding="utf-8").write(re.search(r"<script>(.*)</script>", src, re.S).group(1))
    win = subprocess.run(["wslpath", "-w", OUT], capture_output=True, text=True).stdout.strip()
    harness = r"""
const fs=require('fs'),vm=require('vm'),path=require('path'),dir=process.argv[2];
let buf=new Uint8ClampedArray(128*384*4);
const fakeCtx={getImageData:(x,y,w,h)=>{const d=new Uint8ClampedArray(w*h*4);for(let r=0;r<h;r++)d.set(buf.subarray(((y+r)*128+x)*4,((y+r)*128+x+w)*4),r*w*4);return{data:d,width:w,height:h}},
 putImageData:(img,x,y)=>{for(let r=0;r<img.height;r++)buf.set(img.data.subarray(r*img.width*4,(r+1)*img.width*4),((y+r)*128+x)*4)},
 createImageData:(w,h)=>({data:new Uint8ClampedArray(w*h*4),width:w,height:h})};
const stub=new Proxy(function(){},{get:(t,k)=>k==='getContext'?()=>fakeCtx:k==='checked'?false:k==='value'?'':k===Symbol.toPrimitive?()=>'':stub,set:()=>true,apply:()=>stub,construct:()=>stub});
const ctx=vm.createContext({document:stub,window:stub,localStorage:{getItem:()=>null,setItem:()=>{}},fetch:()=>new Promise(()=>{}),setInterval:()=>0,setTimeout:()=>0,clearTimeout:()=>{},
 console,Math,JSON,Date,Promise,Uint8Array,Uint16Array,Uint8ClampedArray,DataView,Blob:function(){},DecompressionStream:function(){},XMLHttpRequest:function(){},FormData:function(){},alert:()=>{},confirm:()=>true,prompt:()=>null,performance:{now:()=>0}});
vm.runInContext(fs.readFileSync(path.join(dir,'page.js'),'utf8'),ctx);
function decode(b){const w=b[2]|(b[3]<<8),h=b[4]|(b[5]<<8),n=b[6],pal=[];for(let i=0;i<n;i++)pal.push([b[7+i*3],b[8+i*3],b[9+i*3]]);const d=new Uint8ClampedArray(w*h*4);let p=0,o=7+n*3;
 while(o+1<b.length){const v=b[o]|(b[o+1]<<8);o+=2;const c=pal[v>>12];let len=v&4095;while(len--&&p<d.length){d[p++]=c[0];d[p++]=c[1];d[p++]=c[2];d[p++]=255}}return d}
const cases=JSON.parse(fs.readFileSync(path.join(dir,'cases.json'),'utf8'));let bad=0;
ctx.__cases=cases;
for(const c of cases){buf=decode(fs.readFileSync(path.join(dir,c.stem+'.base')));const want=decode(fs.readFileSync(path.join(dir,c.stem+'.want')));
 ctx.__c=c;vm.runInContext("rolls={};ed.bpm=__c.bpm;paintBeat({L:__c.L,m:__c.m,fi:0},beatOf(__c.bpm,__c.grid,__c.frame))",ctx);
 let diff=0,first=-1;for(let i=0;i<want.length;i+=4)if(buf[i]!==want[i]||buf[i+1]!==want[i+1]||buf[i+2]!==want[i+2]){diff++;if(first<0)first=i/4}
 if(diff){bad++;console.log(c.stem,'bpm',c.bpm,'grid',c.grid,'frame',c.frame,'mask',c.m,':',diff,'pixels differ, first at x',first%128,'y',Math.floor(first/128))}}
console.log(cases.length+' cases, '+bad+' with differences');process.exit(bad?1:0);
"""
    open(os.path.join(OUT, "harness.js"), "w").write(harness)
    sys.exit(subprocess.run([NODE, win + "\\harness.js", win]).returncode)


if __name__ == "__main__":
    main()
