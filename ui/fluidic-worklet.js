/* FLUIDIC — audio engine (AudioWorklet).
   Exciter oscillator -> DRIP resonator pair (ported from Continuum Mosaic's DRIP filter) -> FILTER -> amp envelope.
   Every drop the engine fires is reported to the page so the visualiser shows exactly what you hear. */
'use strict';
const TAU = Math.PI * 2;
const clamp = (x, a, b) => (x < a ? a : x > b ? b : x);
const wrap01 = (x) => { x -= Math.floor(x); return x < 0 ? x + 1 : x; };
const MAX_VOICES = 12;

const DEFAULTS = {
  wave: 'saw', oscLevel: 0.6, hold: 0, octave: 0,
  pattern: 'both', cutoff: 900, res: 0.8, key: 0.6,
  ping: 0.85, sweep: 2.2, fall: 0.055, bubble: 0.55, wobble: 0.4,
  chirp: 0, width: 0, density: 0.5, size: 0.5, edge: 0,
  fType: 'off', fCut: 2400, fRes: 0.25, fDrive: 0, fKey: 0.5, fEnv: 0, fA: 0.01, fD: 0.6, fRate: 0.15, fDepth: 0,
  a: 0.004, d: 0.4, s: 0.7, r: 0.5, level: 0.8
};
// post-drip filter: a zero-delay-feedback state-variable filter per voice, stereo, with an optional second stage for 24 dB
function svf(st, o, x, g, k) {
  const a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
  const v3 = x - st[o + 1], v1 = a1 * st[o] + a2 * v3, v2 = st[o + 1] + a2 * st[o] + a3 * v3;
  st[o] = 2 * v1 - st[o]; st[o + 1] = 2 * v2 - st[o + 1];
  return [v1, v2];
}

function makeVoice() {
  return {
    active: false, id: 0, note: 60, vel: 1, gate: false, age: 0, phase: 0, freq: 261.6,
    env: 0, stage: 'idle', fc: 900, pan: 0,
    ic1: 0, ic2: 0, ic1b: 0, ic2b: 0,
    dripT0: 0, dripWait: 0, dripAge: 1e9, dripHit: 0, dripSize: 0.5, dripJit: 0, dripRatio: 0, dripAmp: 0, dripRate: 1,
    dripPh: 0, dripKick: 0, splashN: 0, stW: 0, spW: 0, dripKind: 1, dGuard: 1, dgLvl: 0,
    dripDelay: new Float32Array(32), dripDPos: 0,
    noise: new Float32Array(64), pk: [0, 0, 0],
    fst: new Float64Array(8), fenv: 0, fstage: 0, fg: 0.1,
    gl: 0.7071, gr: 0.7071, pendNote: -1, pendVel: 0, pendOff: false
  };
}

class FluidicProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.p = Object.assign({}, DEFAULTS);
    this.voices = Array.from({ length: MAX_VOICES }, makeVoice);
    this.queue = [];
    this.events = [];            // drops fired since the last report
    this.peak = 0; this.cutFlash = 0; this.lfoPh = 0;
    this.reportEvery = Math.round(sampleRate / 60);
    this.stealK = 1 - Math.exp(-1 / (0.0006 * sampleRate));
    this.reportCount = 0;
    this.port.onmessage = (e) => this.onMessage(e.data);
  }

  onMessage(m) {
    if (!m) return;
    if (m.type === 'params') { Object.assign(this.p, m.params); return; }
    if (m.type === 'on' || m.type === 'off') { this.queue.push(m); return; }
    if (m.type === 'panic') { this.voices.forEach((v) => { v.active = false; v.stage = 'idle'; v.env = 0; }); }
  }

  noteOn(note, vel, noSteal) {
    let v = this.voices.find((x) => !x.active);
    if (!v) {
      if (noSteal) return;                       // ornaments (e.g. Foldspace bubbles) only use free voices
      v = this.voices.reduce((a, b) => (a.env < b.env ? a : b));
      // reuse a sounding voice without a click: fade it for ~2 ms, then start the new note
      v.pendNote = note; v.pendVel = vel; v.pendOff = false; v.stage = 'steal'; v.gate = false; v.id = -1;
      return;
    }
    this.startVoice(v, note, vel);
  }

  startVoice(v, note, vel) {
    const wasIdle = !v.active;
    Object.assign(v, { active: true, id: note, note, vel, gate: true, age: 0, stage: 'attack', dripKick: 1, dripAge: 1e9, dGuard: 1, dgLvl: 0 });
    v.ic1 = v.ic2 = v.ic1b = v.ic2b = 0;
    v.fenv = 0; v.fstage = 1; if (wasIdle) v.fst.fill(0);
    if (!Number.isFinite(v.fst[0] + v.fst[1] + v.fst[2] + v.fst[3] + v.fst[4] + v.fst[5] + v.fst[6] + v.fst[7])) v.fst.fill(0);
    v.freq = 440 * Math.pow(2, (note + 12 * this.p.octave - 69) / 12);
    for (let i = 0; i < 64; i++) v.noise[i] = Math.random() * 2 - 1;   // HOLD: a random cycle looped at the note's pitch
    v.pan = clamp((note - 60) / 36, -0.6, 0.6);
    v.fc = clamp(this.p.cutoff * Math.pow(2, (note - 60) / 12 * this.p.key), 30, sampleRate * 0.45);
    const angle = (v.pan * 0.6 + 1) * 0.25 * Math.PI; v.gl = Math.cos(angle); v.gr = Math.sin(angle);
  }

  noteOff(note) {
    this.voices.forEach((v) => {
      if (v.active && v.id === note && v.gate) { v.gate = false; v.stage = 'release'; }
      else if (v.stage === 'steal' && v.pendNote === note) v.pendOff = true;
    });
  }

  drop(v, size, kind) {          // report a drop to the visualiser (x: 0..1 across the stereo field)
    if (this.events.length > 64) return;
    const p = this.p;
    const spread = 0.12 + p.width * 0.38;
    this.events.push({ x: clamp(0.5 + v.pan * 0.45 + v.dripJit * spread, 0.03, 0.97), size, kind, note: v.note, sweep: p.sweep, chirp: p.chirp });
  }

  env(v, ka, kd, kr) {
    const p = this.p;
    if (v.stage === 'attack') { v.env += (1.02 - v.env) * ka; if (v.env >= 1) { v.env = 1; v.stage = 'decay'; } }
    else if (v.stage === 'decay') { v.env += (p.s - v.env) * kd; }
    else if (v.stage === 'release') { v.env += (0 - v.env) * kr; if (v.env < 0.0004) { v.env = 0; v.stage = 'idle'; v.active = false; } }
    else if (v.stage === 'steal') {
      v.env -= v.env * this.stealK;
      if (v.env < 0.001) {
        const off = v.pendOff; v.env = 0; v.active = false; this.startVoice(v, v.pendNote, v.pendVel); v.pendNote = -1;
        if (off) { v.gate = false; v.stage = 'release'; }
      }
    }
    return v.env;
  }

  osc(v, sr) {
    const p = this.p, ph = v.phase, dt = v.freq / sr;
    v.phase = wrap01(ph + dt);
    let y;
    switch (p.wave) {
      case 'sine': y = Math.sin(TAU * ph); break;
      case 'square': y = ph < 0.5 ? 1 : -1; break;
      case 'noise': {
        const wn = Math.random() * 2 - 1, hold = clamp(p.hold, 0, 1);
        y = wn * (1 - hold) + v.noise[(ph * 64) | 0] * hold; break;
      }
      default: { // saw with a light polyBLEP
        y = 2 * ph - 1;
        if (ph < dt) { const t = ph / dt; y -= t + t - t * t - 1; }
        else if (ph > 1 - dt) { const t = (ph - 1) / dt; y -= t * t + t + t + 1; }
      }
    }
    return y * p.oscLevel;
  }

  process(inputs, outputs) {
    const out = outputs[0], L = out[0], R = out[1] || out[0], n = L.length, sr = sampleRate, p = this.p;
    while (this.queue.length) { const m = this.queue.shift(); if (m.type === 'on') this.noteOn(m.note, m.vel || 0.8, m.noSteal); else this.noteOff(m.note); }
    const rawMode = p.pattern;
    const dripStyle = rawMode === 'splash' || rawMode === 'stream' ? rawMode : '';
    const dripMode = rawMode === 'even' || rawMode === 'rain' ? rawMode : dripStyle ? 'rain' : 'both';
    const storm = rawMode === 'storm';
    const dripRes = clamp(p.res, 0, 1), dripQ = 6 + dripRes * dripRes * 480;
    const ping = clamp(p.ping, 0, 1), sweep = clamp(p.sweep, -4, 4), fallT = clamp(p.fall, 0.02, 0.45);
    const bubble = clamp(p.bubble, 0, 1), wob = clamp(p.wobble, 0, 1);
    let chirp = clamp(p.chirp, 0, 1), dwidth = clamp(p.width, 0, 1);
    const density = clamp(p.density, 0, 1), dsize = clamp(p.size, 0, 1), dguard = clamp(p.edge, 0, 1);
    if (rawMode === '3d') { chirp = 1; if (dwidth < 0.8) dwidth = 0.8; }
    const dens = density === 0.5 ? 1 : Math.pow(2, (0.5 - density) * 4);
    const norm = 1 / Math.sqrt(1 + dripQ * 0.012);
    let peak = 0;
    const fType = p.fType, fOn = fType && fType !== 'off', fRes = clamp(p.fRes, 0, 1), fk = 2 - 1.96 * fRes, fk24 = 2 - 1.6 * fRes, bpGain = Math.pow(fk, 0.7) * 1.4;
    const fDrive = clamp(p.fDrive, 0, 1), fDg = 1 + fDrive * 5, fDn = 1 / (1 + fDrive * 1.6);
    const fAk = 1 - Math.exp(-1 / (Math.max(0.0008, p.fA) * sr)), fDk = 1 - Math.exp(-1 / (Math.max(0.005, p.fD) * sr));
    const lfoInc = clamp(p.fRate, 0.01, 20) / sr, fDepth = clamp(p.fDepth, 0, 1) * 3, fEnvOct = clamp(p.fEnv, -1, 1) * 5;
    // per-block constants (they only change when a dial moves)
    const ek = (t) => 1 - Math.exp(-1 / (Math.max(0.0008, t) * sr)), ka = ek(p.a), kd = ek(p.d), kr = ek(p.r);
    for (const v of this.voices) {
      if (!v.active) continue;
      v.fc = clamp(p.cutoff * Math.pow(2, (v.note - 60) / 12 * p.key), 30, sr * 0.45);
      const angle = (v.pan * 0.6 + 1) * 0.25 * Math.PI; v.gl = Math.cos(angle); v.gr = Math.sin(angle);
    }

    for (let i = 0; i < n; i++) {
      let busL = 0, busR = 0;
      this.lfoPh += lfoInc; if (this.lfoPh >= 1) this.lfoPh -= 1;
      const lfo = Math.sin(this.lfoPh * TAU);
      for (const v of this.voices) {
        if (!v.active) continue;
        const env = this.env(v, ka, kd, kr);
        if (!v.active) continue;
        v.age += 1;
        const mix = this.osc(v, sr);

        if (v.dripKick) {
          v.dripKick = 0;
          if (dripMode !== 'even') { v.dripWait = 1; v.splashN = 3 + ((Math.random() * 4) | 0); if (storm) { v.stW = 1 + Math.random() * fallT * sr; v.spW = fallT * sr * (0.5 + Math.random() * 2); v.dripKind = 0.55; } }
          if (dripMode !== 'rain') { v.dripT0 = v.age; v.dripJit = (Math.random() * 2 - 1) * 0.3; this.drop(v, 1, 'note'); }
        }
        const dAge = v.age - v.dripT0;
        if (storm && v.gate) {
          v.dripWait -= 1; v.stW -= 1; v.spW -= 1;
          let kind = 0;
          if (v.dripWait <= 0) { kind = 1; const r = Math.random(); v.dripWait = Math.max(12, fallT * sr * (0.28 + r * r * 7.4) * dens * 1.6); }
          else if (v.spW <= 0) {
            kind = 2; const r = Math.random();
            if (v.splashN > 0) { v.splashN -= 1; v.spW = Math.max(12, fallT * sr * (0.1 + r * 0.28) * dens); }
            else { v.splashN = 2 + ((Math.random() * 4) | 0); v.spW = Math.max(12, fallT * sr * (3 + r * 10) * dens); }
          } else if (v.stW <= 0) { kind = 3; const r = Math.random(); v.stW = Math.max(12, fallT * sr * (0.1 + r * 0.4) * dens * 1.5); }
          if (kind) {
            v.dripAge = 0; v.dripHit = 3;
            const sr2 = Math.random();
            v.dripSize = (0.12 + sr2 * sr2 * 0.88) * (kind === 3 ? 0.4 : kind === 2 ? 0.8 : 1);
            if (dsize !== 0.5) v.dripSize = clamp(v.dripSize * Math.pow(2, (dsize - 0.5) * 2.4), 0.04, 1.5);
            v.dripJit = Math.random() * 2 - 1; v.dripRatio = Math.random() * 0.7 - 0.22;
            v.dripAmp = Math.random() * 2 - 1; v.dripRate = 0.25 + Math.random() * 0.9;
            v.dripKind = kind === 1 ? 0.55 : kind === 2 ? 0.45 : 0.28;
            this.drop(v, v.dripSize, kind === 1 ? 'rain' : kind === 2 ? 'splash' : 'stream');
          }
        }
        if (dripMode !== 'even' && v.gate && !storm) {
          v.dripWait -= 1;
          if (v.dripWait <= 0) {
            const g = Math.random();
            let gap = fallT * sr * (0.28 + g * g * 7.4) * dens;
            if (dripStyle === 'stream') gap = fallT * sr * (0.06 + g * 0.24) * dens;
            else if (dripStyle === 'splash') {
              if (v.splashN > 0) { v.splashN -= 1; gap = fallT * sr * (0.1 + g * 0.28) * dens; }
              else { v.splashN = 2 + ((Math.random() * 5) | 0); gap = fallT * sr * (2.5 + g * 9) * dens; }
            }
            v.dripWait = gap < 12 ? 12 : gap;
            v.dripAge = 0; v.dripHit = 3;
            const sr2 = Math.random();
            v.dripSize = 0.12 + sr2 * sr2 * 0.88;
            if (dsize !== 0.5) v.dripSize = clamp(v.dripSize * Math.pow(2, (dsize - 0.5) * 2.4), 0.04, 1.5);
            if (dripStyle === 'stream') v.dripSize *= 0.45;
            v.dripJit = Math.random() * 2 - 1; v.dripRatio = Math.random() * 0.7 - 0.22;
            v.dripAmp = Math.random() * 2 - 1; v.dripRate = 0.25 + Math.random() * 0.9;
            this.drop(v, v.dripSize, dripStyle || 'rain');
          }
        }
        if (v.dripHit > 0) v.dripHit -= 1;
        v.dripAge += 1;

        let fall, swept, wobble, dripRatio;
        if (dripMode === 'rain') {
          fall = Math.exp(-v.dripAge / (fallT * sr * (0.45 + v.dripSize * 0.9)));
          if (chirp > 0) fall *= Math.pow(fall, chirp);
          const scatter = v.dripJit * (0.35 + Math.abs(sweep) * 0.2);
          swept = v.fc * Math.pow(2, sweep * fall + scatter * (0.45 + 0.55 * fall));
          wobble = Math.sin(TAU * v.dripPh) * wob * (0.1 + 0.32 * v.dripSize);
          dripRatio = 1.28 + v.dripRatio + 0.22;
        } else if (dripMode === 'both') {
          fall = Math.exp(-dAge / (fallT * sr));
          let dropFall = Math.exp(-v.dripAge / (fallT * sr * (0.5 + v.dripSize)));
          if (chirp > 0) { fall *= Math.pow(fall, chirp); dropFall *= Math.pow(dropFall, chirp); }
          const scatter = v.dripJit * dropFall * (0.22 + Math.abs(sweep) * 0.12);
          swept = v.fc * Math.pow(2, sweep * fall + scatter);
          wobble = Math.sin(TAU * v.dripPh) * wob * 0.28 * (1 + 0.55 * v.dripAmp * dropFall);
          dripRatio = 1.51 + v.dripRatio * dropFall * 0.45;
        } else {
          fall = Math.exp(-dAge / (fallT * sr));
          if (chirp > 0) fall *= Math.pow(fall, chirp);
          swept = v.fc * Math.pow(2, sweep * fall);
          wobble = Math.sin(TAU * v.dripPh) * wob * 0.28;
          dripRatio = 1.51;
        }
        const fc = clamp(swept * (1 + wobble), 32, sr * 0.45);
        v.dripPh = wrap01(v.dripPh + (fc * (0.42 + dripRes * 0.5) * (dripMode === 'rain' ? v.dripRate : 1)) / sr);
        const g = Math.tan(Math.PI * fc / sr), k = 1 / dripQ, a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const gb = Math.tan(Math.PI * clamp(fc * dripRatio, 40, sr * 0.45) / sr), kb = 1 / Math.max(1.6, dripQ * 0.62);
        const b1 = 1 / (1 + gb * (gb + kb)), b2 = gb * b1, b3 = gb * b2;
        let exc = mix;
        if (dripMode !== 'rain' && dAge < 5 && ping > 0.001) exc += (dAge < 2 ? 0.9 : 0.16) * ping * (storm ? 0.6 : 1);
        if (dripMode !== 'even' && v.dripHit > 0 && ping > 0.001) exc += (v.dripHit > 1 ? 0.8 : 0.18) * ping * v.dripSize * (storm ? v.dripKind : 1) * v.dGuard;
        const v3 = exc - v.ic2, v1 = a1 * v.ic1 + a2 * v3, v2 = v.ic2 + a2 * v.ic1 + a3 * v3;
        v.ic1 = 2 * v1 - v.ic1; v.ic2 = 2 * v2 - v.ic2;
        if (!Number.isFinite(v.ic1) || v.ic1 > 6 || v.ic1 < -6) { v.ic1 = 0; v.ic2 = 0; }
        const w3 = exc - v.ic2b, w1 = b1 * v.ic1b + b2 * w3, w2 = v.ic2b + b2 * v.ic1b + b3 * w3;
        v.ic1b = 2 * w1 - v.ic1b; v.ic2b = 2 * w2 - v.ic2b;
        if (!Number.isFinite(v.ic1b) || v.ic1b > 6 || v.ic1b < -6) { v.ic1b = 0; v.ic2b = 0; }
        let shaped = (v1 * 0.74 + v2 * 0.28 + w1 * bubble) * norm;
        // EDGE: off = runs as hot as it goes; turning it up pulls back from overload. A real blow-up is always cut.
        const pk = Math.abs(shaped);
        v.dgLvl = pk > v.dgLvl ? pk : v.dgLvl * 0.9997;
        if (dguard > 0.001) {
          if (v.dgLvl > 2.6 - dguard * 1.4) v.dGuard = Math.max(0.4 - dguard * 0.2, v.dGuard * 0.997);
          else if (v.dGuard < 1) v.dGuard = Math.min(1, v.dGuard + 0.00002);
        } else v.dGuard = 1;
        if (v.dgLvl > 6) { v.ic1 *= 0.5; v.ic2 *= 0.5; v.ic1b *= 0.5; v.ic2b *= 0.5; v.dgLvl *= 0.5; this.cutFlash = 1; }
        if (v.dGuard !== 1) shaped *= v.dGuard;
        if (v.dgLvl > peak) peak = v.dgLvl;
        let sl = shaped, srr = shaped;
        if (dwidth > 0.001) {
          const pri = (v1 * 0.74 + v2 * 0.28) * norm, sec = w1 * bubble * norm, dl = v.dripDelay;
          dl[v.dripDPos] = sec;
          const tap = (v.dripDPos - (2 + ((dwidth * (6 + bubble * 14 + Math.abs(v.dripJit) * 5)) | 0)) + 32) & 31;
          v.dripDPos = (v.dripDPos + 1) & 31;
          sl = (pri + sec * 0.9) * v.dGuard; srr = (pri + (sec * (1 - dwidth) + dl[tap] * dwidth) * 0.9) * v.dGuard;
          if (!Number.isFinite(sl) || !Number.isFinite(srr)) { dl.fill(0); sl = srr = 0; }
        }
        if (fOn) {
          // filter envelope: attack up, decay back to the dial's cutoff
          if (v.fstage === 1) { v.fenv += (1.02 - v.fenv) * fAk; if (v.fenv >= 1) { v.fenv = 1; v.fstage = 2; } }
          else v.fenv += (0 - v.fenv) * fDk;
          if ((i & 7) === 0) {
            const oct = p.fKey * (v.note - 60) / 12 + fEnvOct * v.fenv + fDepth * lfo;
            const fcut = clamp(p.fCut * Math.pow(2, oct), 20, sr * 0.45);
            v.fg = Math.tan(Math.PI * fcut / sr);
          }
          let xl = sl, xr = srr;
          if (fDrive > 0.001) { xl = Math.tanh(xl * fDg) * fDn; xr = Math.tanh(xr * fDg) * fDn; }
          const st = v.fst, g = v.fg, k = fType === 'lp24' ? fk24 : fk;
          const pick = (x, o) => {
            const [b, l] = svf(st, o, x, g, k);
            if (fType === 'lp' ) return l;
            if (fType === 'lp24') return svf(st, o + 4, l, g, k)[1];
            if (fType === 'hp') return x - k * b - l;
            if (fType === 'bp') return b * bpGain;
            return x - k * b;                       // notch
          };
          sl = pick(xl, 0); srr = pick(xr, 2);
          if (!Number.isFinite(sl) || !Number.isFinite(srr)) { v.fst.fill(0); sl = srr = 0; }
        }
        const amp = env * (0.3 + 0.7 * v.vel) * p.level;
        busL += Math.tanh(sl * amp * 1.35) * v.gl;
        busR += Math.tanh(srr * amp * 1.35) * v.gr;
      }
      L[i] = busL * 0.55; if (R !== L) R[i] = busR * 0.55;
    }
    this.peak = Math.max(this.peak * 0.9, peak);
    if (++this.reportCount * n >= this.reportEvery) {
      this.reportCount = 0;
      this.port.postMessage({ type: 'frame', events: this.events, peak: this.peak, cut: this.cutFlash, voices: this.voices.filter((v) => v.active).length });
      this.events = []; this.cutFlash = 0;
    }
    return true;
  }
}
registerProcessor('fluidic', FluidicProcessor);
