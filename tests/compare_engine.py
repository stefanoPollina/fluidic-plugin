"""Parity test: C++ engine vs the web prototype's worklet, same seed, same notes, sample by sample.
Needs: Chrome + Playwright, a local server serving fluidic-worklet.js, and the compiled render_engine."""
import asyncio, subprocess, struct, sys, os
from playwright.async_api import async_playwright
SERVER = os.environ.get('FLUIDIC_WEB', 'http://localhost:8766')
RENDER = os.environ.get('RENDER_ENGINE', '/tmp/render_engine')
JS = """async ([params, notes, seed, secs]) => {
  let src = await (await fetch('/fluidic-worklet.js')).text();
  src = `let __s=${seed}; Math.random=function(){__s|=0;__s=__s+0x6D2B79F5|0;let t=Math.imul(__s^__s>>>15,1|__s);t=t+Math.imul(t^t>>>7,61|t)^t;return((t^t>>>14)>>>0)/4294967296;};` + src;
  const ctx = new OfflineAudioContext(2, 48000*secs, 48000);
  await ctx.audioWorklet.addModule(URL.createObjectURL(new Blob([src],{type:'text/javascript'})));
  const n = new AudioWorkletNode(ctx,'fluidic',{outputChannelCount:[2]}); n.connect(ctx.destination);
  n.port.postMessage({type:'params', params});
  for (const k of notes) n.port.postMessage({type:'on', note:k, vel:.8});
  await new Promise(r=>setTimeout(r,50));
  const b = await ctx.startRendering(); const L=b.getChannelData(0), R=b.getChannelData(1), o=[];
  for (let i=0;i<L.length;i++){o.push(L[i]);o.push(R[i]);} return o;
}"""
CASES = {
 'default (both, saw)': {},
 'even square': {'pattern':'even','wave':'square'},
 'rain sine lp24 width': {'wave':'sine','pattern':'rain','width':.7,'fType':'lp24','fDepth':.35,'fEnv':.25,'fRate':2},
 'splash noise+hold hp': {'pattern':'splash','wave':'noise','hold':.5,'fType':'hp','a':.3,'r':.1},
 'stream notch drive': {'pattern':'stream','fType':'notch','fDrive':.6,'size':.8},
 '3d bp chirp': {'pattern':'3d','fType':'bp','fRes':.7,'chirp':.6},
 'storm edge hot': {'pattern':'storm','edge':.6,'res':.97,'ping':1,'bubble':1,'density':1},
 'storm edge off': {'pattern':'storm','edge':0,'res':.97,'ping':1,'bubble':1},
 'octave/key/env': {'octave':-1,'key':.9,'fType':'lp','fEnv':-.8,'fA':.05,'fD':.3,'s':.2,'d':.1},
 '16 notes: voice reuse': {'_notes':[40,43,47,50,52,55,59,62,64,67,71,74,76,79,83,86],'pattern':'rain','r':2},
}
NOTES=[48,55,62,67]
async def main():
  fails=0
  async with async_playwright() as p:
    b=await p.chromium.launch(executable_path='/usr/bin/google-chrome'); pg=await b.new_page()
    await pg.goto(SERVER + '/index.html')
    for name,prm in CASES.items():
      prm=dict(prm); notes=prm.pop('_notes', NOTES)
      web=await pg.evaluate(JS, [prm, notes, 12345, 4])
      with open('/tmp/case.txt','w') as f:
        for k,v in prm.items(): f.write(f'{k} {v}\n')
        f.write('notes '+' '.join(map(str,notes))+'\nseconds 4\nseed 12345\n')
      subprocess.run([RENDER,'/tmp/case.txt','/tmp/case.raw'],check=True)
      raw=open('/tmp/case.raw','rb').read(); cpp=struct.unpack(f'{len(raw)//4}f',raw)
      diff=max(abs(a-c) for a,c in zip(web,cpp)); rms=(sum(a*a for a in web)/len(web))**.5
      ok = diff < 1e-4
      fails += not ok
      print(f"{'OK  ' if ok else 'FAIL'} {name:24s} max difference {diff:.2e}   (signal rms {rms:.3f})")
    await b.close()
  sys.exit(1 if fails else 0)
asyncio.run(main())
