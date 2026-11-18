"""Parity test: C++ FX engine vs the web prototype's fluidic-fx-worklet.js, fed the same breakbeat, same seed."""
import asyncio, subprocess, struct, sys, os
from playwright.async_api import async_playwright
SERVER = os.environ.get('FLUIDIC_WEB', 'http://localhost:8766'); RENDER = os.environ.get('RENDER_FX', '/tmp/render_fx')
JS = """async ([prm, seed, secs]) => {
  let src = await (await fetch('/fluidic-fx-worklet.js')).text();
  src = `let __s=${seed}; Math.random=function(){__s|=0;__s=__s+0x6D2B79F5|0;let t=Math.imul(__s^__s>>>15,1|__s);t=t+Math.imul(t^t>>>7,61|t)^t;return((t^t>>>14)>>>0)/4294967296;};` + src;
  const sr = 48000, ctx = new OfflineAudioContext(2, sr*secs, sr);
  await ctx.audioWorklet.addModule(URL.createObjectURL(new Blob([src],{type:'text/javascript'})));
  const b = Fluidic.makeBreak(sr, prm.bpm || 120), buf = ctx.createBuffer(2, b.length, sr); buf.copyToChannel(b.L, 0); buf.copyToChannel(b.R, 1);
  const s = ctx.createBufferSource(); s.buffer = buf; s.loop = true;
  const fx = new AudioWorkletNode(ctx, 'fluidic-fx', { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2] });
  s.connect(fx).connect(ctx.destination); fx.port.postMessage({ type: 'params', params: prm }); s.start();
  await new Promise(r => setTimeout(r, 50));
  const out = await ctx.startRendering();
  // the exact input the worklet saw: the looped break
  const inp = [], o = [], L = out.getChannelData(0), R = out.getChannelData(1);
  for (let i = 0; i < L.length; i++) { const j = i % b.length; inp.push(b.L[j], b.R[j]); o.push(L[i], R[i]); }
  return [inp, o];
}"""
CASES = {
 'even 1/16 (Break Drips)': {'pattern':'even','rate':4},
 'rain': {'pattern':'rain','density':.6},
 'splash': {'pattern':'splash'},
 'stream 1/32': {'pattern':'stream','rate':8},
 'storm': {'pattern':'storm','density':.8},
 '3d 1/8': {'pattern':'3d','rate':2},
 'chop + mix wet': {'pattern':'even','rate':2,'chop':1,'mix':.8},
 'hits trigger (sense)': {'pattern':'rain','density':.1,'sense':.7},
 'lp24 env + lfo': {'pattern':'both','fType':'lp24','fEnv':.6,'fCut':600,'fDepth':.4,'fRate':3},
 'storm hot, edge': {'pattern':'storm','res':.97,'ping':1,'bubble':1,'density':1,'drive':4,'edge':.8},
 '100 bpm, bp drive': {'pattern':'both','bpm':100,'fType':'bp','fRes':.7,'fDrive':.5},
}
async def main():
  fails = 0
  async with async_playwright() as p:
    b=await p.chromium.launch(executable_path='/usr/bin/google-chrome'); pg=await b.new_page()
    await pg.goto(SERVER + '/index.html')
    for name, prm in CASES.items():
      inp, web = await pg.evaluate(JS, [prm, 12345, 4])
      open('/tmp/fxin.raw','wb').write(struct.pack(f'{len(inp)}f', *inp))
      with open('/tmp/fxcase.txt','w') as f:
        for k,v in prm.items(): f.write(f'{k} {v}\n')
        f.write('seed 12345\n')
      subprocess.run([RENDER, '/tmp/fxcase.txt', '/tmp/fxin.raw', '/tmp/fxout.raw'], check=True)
      raw = open('/tmp/fxout.raw','rb').read(); cpp = struct.unpack(f'{len(raw)//4}f', raw)
      diff = max(abs(a-c) for a,c in zip(web,cpp)); rms = (sum(a*a for a in web)/len(web))**.5
      ok = diff < 1e-4; fails += not ok
      print(f"{'OK  ' if ok else 'FAIL'} {name:26s} max difference {diff:.2e}   (signal rms {rms:.3f})")
    await b.close()
  sys.exit(1 if fails else 0)
asyncio.run(main())
