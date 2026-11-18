"""Full-chain check: C++ engine + effects bus vs the web prototype's Web Audio graph (same seeds).
The limiters differ slightly by design (Chrome's DynamicsCompressor also delays by ~6 ms), so this
compares loudness over time (100 ms windows) rather than individual samples."""
import asyncio, subprocess, struct, math, os
from playwright.async_api import async_playwright
SERVER = os.environ.get('FLUIDIC_WEB', 'http://localhost:8766'); RENDER = os.environ.get('RENDER_FULL', '/tmp/rtest/build/fluidic_render')
JS = """async ([params, fxp, notes, seed, revSeed, secs]) => {
  const mul = (s) => function(){s|=0;s=s+0x6D2B79F5|0;let t=Math.imul(s^s>>>15,1|s);t=t+Math.imul(t^t>>>7,61|t)^t;return((t^t>>>14)>>>0)/4294967296;};
  let src = await (await fetch('/fluidic-worklet.js')).text();
  src = `let __s=${seed}; Math.random=function(){__s|=0;__s=__s+0x6D2B79F5|0;let t=Math.imul(__s^__s>>>15,1|__s);t=t+Math.imul(t^t>>>7,61|t)^t;return((t^t>>>14)>>>0)/4294967296;};` + src;
  const sr=48000, ctx = new OfflineAudioContext(2, sr*secs, sr);
  await ctx.audioWorklet.addModule(URL.createObjectURL(new Blob([src],{type:'text/javascript'})));
  const node = new AudioWorkletNode(ctx,'fluidic',{numberOfInputs:0,numberOfOutputs:1,outputChannelCount:[2]});
  // the prototype's graph (see start() in index.html), with values set directly
  const master = ctx.createGain(), dry = ctx.createGain(), dSend = ctx.createGain(), rSend = ctx.createGain();
  const dL = ctx.createDelay(2), dR = ctx.createDelay(2), fb = ctx.createGain(), merge = ctx.createChannelMerger(2), tone = ctx.createBiquadFilter();
  tone.type='lowpass'; tone.frequency.value=5200;
  const rev = ctx.createConvolver(), limiter = ctx.createDynamicsCompressor();
  limiter.threshold.value=-2; limiter.ratio.value=20; limiter.attack.value=0.002; limiter.release.value=0.12; limiter.knee.value=2;
  node.connect(dry).connect(master); node.connect(dSend).connect(tone).connect(dL);
  dL.connect(dR); dR.connect(fb).connect(dL); dL.connect(merge,0,0); dR.connect(merge,0,1); merge.connect(master);
  node.connect(rSend).connect(rev).connect(master); master.connect(limiter).connect(ctx.destination);
  dSend.gain.value=fxp.delayMix; dL.delayTime.value=fxp.delayTime; dR.delayTime.value=fxp.delayTime; fb.gain.value=fxp.delayFb; rSend.gain.value=fxp.revMix;
  const R = mul(revSeed), len=Math.max(1,Math.floor(sr*fxp.revSize)), buf=ctx.createBuffer(2,len,sr);
  for (let c=0;c<2;c++){const d=buf.getChannelData(c); for(let i=0;i<len;i++) d[i]=(R()*2-1)*Math.pow(1-i/len,2.6);}
  rev.buffer = buf;
  node.port.postMessage({type:'params', params});
  for (const k of notes) node.port.postMessage({type:'on', note:k, vel:.8});
  await new Promise(r=>setTimeout(r,50));
  const b = await ctx.startRendering(); const L=b.getChannelData(0), Rr=b.getChannelData(1), o=[];
  for (let i=0;i<L.length;i++){o.push(L[i]);o.push(Rr[i]);} return o;
}"""
CASES_ALL = {
 'Sine Meditation': ({'wave':'sine','pattern':'rain','oscLevel':.7,'cutoff':520,'res':.88,'ping':.7,'sweep':1.2,'fall':.12,'bubble':.4,'wobble':.5,'chirp':.2,'density':.32,'width':.7,'fType':'lp24','fCut':900,'fRes':.35,'fKey':.4,'fEnv':.25,'fA':1.2,'fD':4,'fRate':.07,'fDepth':.35,'a':1.2,'s':.9,'r':3.5},
                     {'delayMix':.25,'delayTime':.5,'delayFb':.45,'revMix':.55,'revSize':6.5}),
 'Init Drop (defaults)': ({}, {'delayMix':.18,'delayTime':.28,'delayFb':.38,'revMix':.24,'revSize':2.2}),
 'delay only, quiet': ({'level':.3}, {'delayMix':.6,'delayTime':.3,'delayFb':.6,'revMix':0,'revSize':1}),
 'reverb only': ({'pattern':'splash','wave':'noise','hold':.05}, {'delayMix':0,'delayTime':.3,'delayFb':0,'revMix':.8,'revSize':4}),
 'hot: storm into limiter': ({'pattern':'storm','res':.97,'ping':1,'bubble':1,'level':1}, {'delayMix':.4,'delayTime':.2,'delayFb':.7,'revMix':.5,'revSize':3}),
}
import sys
DEBUG = {
 'dry only (quiet)': ({'level':.3}, {'delayMix':0,'delayTime':.3,'delayFb':0,'revMix':0,'revSize':1}),
 'delay, no feedback': ({'level':.3}, {'delayMix':.6,'delayTime':.3,'delayFb':0,'revMix':0,'revSize':1}),
 'delay with feedback': ({'level':.3}, {'delayMix':.6,'delayTime':.3,'delayFb':.6,'revMix':0,'revSize':1}),
 'reverb only (quiet)': ({'level':.3}, {'delayMix':0,'delayTime':.3,'delayFb':0,'revMix':.8,'revSize':2}),
}
CASES = DEBUG if '--debug' in sys.argv else CASES_ALL
NOTES=[48,55,62,67]
def windows(x, n=4800):
    L=x[0::2]; R=x[1::2]; out=[]
    for i in range(0,len(L)-n,n):
        s=sum(L[j]*L[j]+R[j]*R[j] for j in range(i,i+n))/(2*n); out.append(10*math.log10(s+1e-12))
    return out
async def main():
  async with async_playwright() as p:
    b=await p.chromium.launch(executable_path='/usr/bin/google-chrome'); pg=await b.new_page()
    await pg.goto(SERVER + '/index.html')
    for name,(prm,fxp) in CASES.items():
      web=await pg.evaluate(JS, [prm, fxp, NOTES, 12345, 99, 6])
      with open('/tmp/full.txt','w') as f:
        for k,v in {**prm, **fxp}.items(): f.write(f'{k} {v}\n')
        f.write('notes '+' '.join(map(str,NOTES))+'\nseconds 6\nseed 12345\nrevseed 99\n')
      subprocess.run([RENDER,'/tmp/full.txt','/tmp/full.raw'],check=True)
      raw=open('/tmp/full.raw','rb').read(); cpp=struct.unpack(f'{len(raw)//4}f',raw)
      a=windows(web); c=windows(cpp)
      diffs=[abs(x-y) for x,y in zip(a,c) if x>-60]
      tot=lambda x: 10*math.log10(sum(v*v for v in x)/len(x)+1e-12)
      pk=lambda x: max(abs(v) for v in x)
      print(f"{name:26s} overall level web {tot(web):6.2f} dB / C++ {tot(cpp):6.2f} dB | 100ms windows: median diff {sorted(diffs)[len(diffs)//2]:.2f} dB, worst {max(diffs):.2f} dB | peak web {pk(web):.3f} / C++ {pk(cpp):.3f}")
    await b.close()
asyncio.run(main())
