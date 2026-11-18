/* FLUIDIC FX — the drip engine as an effect (AudioWorklet).
   Incoming audio (a breakbeat, a vocal, anything) is what the drip resonators ring with.
   A tempo-locked scheduler drops "drips" in the rhythm of the chosen pattern; each drip is a resonant,
   pitch-swept slice of the input. CHOP gates the dry signal to the drip rhythm, so the pattern re-cuts it.
   The input's own hits can trigger drips too (SENSE), and MIDI/keys set the drips' pitch. */
'use strict';
const TAU = Math.PI * 2;
const clamp = (x, a, b) => (x < a ? a : x > b ? b : x);
const wrap01 = (x) => { x -= Math.floor(x); return x < 0 ? x + 1 : x; };
const DROPS = 12;

const DEFAULTS = {
  pattern: 'even', rate: 4, bpm: 120, density: 0.5, sense: 0,
  cutoff: 900, res: 0.8, ping: 0.6, sweep: 2.2, fall: 0.08, bubble: 0.55, wobble: 0.3,
  chirp: 0, width: 0.4, size: 0.5, edge: 0, key: 1,
  mix: 0.5, chop: 0, drive: 1,
  fType: 'off', fCut: 2400, fRes: 0.25, fDrive: 0, fEnv: 0, fA: 0.005, fD: 0.25, fRate: 0.15, fDepth: 0,
  level: 0.8
};
function svf(st, o, x, g, k) {
  const a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
  const v3 = x - st[o + 1], v1 = a1 * st[o] + a2 * v3, v2 = st[o + 1] + a2 * st[o] + a3 * v3;
  st[o] = 2 * v1 - st[o]; st[o + 1] = 2 * v2 - st[o + 1];
  return [v1, v2];
}
const makeDrop = () => ({ active: false, age: 0, size: 0.5, jit: 0, ratio: 0, amp: 0, rate: 1, fc: 900, ph: 0, gl: 0.707, gr: 0.707,
  ic1: 0, ic2: 0, ic1b: 0, ic2b: 0, env: 0, len: 1, dGuard: 1, dgLvl: 0 });

class FluidicFxProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.p = Object.assign({}, DEFAULTS);
    this.drops = Array.from({ length: DROPS }, makeDrop);
    this.clock = 0; this.step = 0; this.pending = [];
    this.fast = 0; this.slow = 0; this.refractory = 0;
    this.gate = 0; this.held = []; this.lfoPh = 0;
    this.fst = new Float64Array(8); this.fenv = 0; this.fstage = 0; this.fg = 0.1;
    this.events = []; this.peak = 0; this.cutFlash = 0;
    this.reportEvery = Math.round(sampleRate / 60); this.reportCount = 0;
    this.port.onmessage = (e) => this.onMessage(e.data);
  }

  onMessage(m) {
    if (!m) return;
    if (m.type === 'params') Object.assign(this.p, m.params);
    else if (m.type === 'sync') { this.clock = 0; this.step = 0; this.pending.length = 0; }   // loop restarted: realign the grid
    else if (m.type === 'on') { this.held = this.held.filter((n) => n !== m.note); this.held.push(m.note); this.spawn(0.95, 'note', m.note); }
    else if (m.type === 'off') this.held = this.held.filter((n) => n !== m.note);
    else if (m.type === 'panic') { this.held = []; this.pending.length = 0; this.drops.forEach((d) => { d.active = false; }); }
  }

  // a new drip; its pitch follows the held key (if any) else the PITCH dial
  spawn(size, kind, note) {
    const p = this.p, sr = sampleRate;
    let d = this.drops.find((x) => !x.active);
    if (!d) d = this.drops.reduce((a, b) => (a.env < b.env ? a : b));
    const n = note != null ? note : this.held.length ? this.held[this.held.length - 1] : 60;
    const dsize = clamp(p.size, 0, 1);
    size = clamp(size * Math.pow(2, (dsize - 0.5) * 2.4), 0.05, 1.5);
    Object.assign(d, { active: true, age: 0, size, jit: Math.random() * 2 - 1, ratio: Math.random() * 0.7 - 0.22,
      amp: Math.random() * 2 - 1, rate: 0.25 + Math.random() * 0.9, ic1: 0, ic2: 0, ic1b: 0, ic2b: 0, dGuard: 1, dgLvl: 0, env: 0 });
    d.fc = clamp(p.cutoff * Math.pow(2, (n - 60) / 12 * clamp(p.key, 0, 1)), 30, sr * 0.45);
    let width = clamp(p.width, 0, 1); if (p.pattern === '3d' && width < 0.8) width = 0.8;
    const angle = (clamp(d.jit * width, -1, 1) + 1) * 0.25 * Math.PI; d.gl = Math.cos(angle); d.gr = Math.sin(angle);
    d.len = clamp(p.fall, 0.02, 0.45) * sr * (1.5 + size * 4);
    this.fenv = 0; this.fstage = 1;                       // the filter envelope follows the drips
    if (this.events.length < 64) this.events.push({ x: clamp(0.5 + d.jit * width * 0.45, 0.03, 0.97), size, kind, note: n, sweep: p.sweep, chirp: p.chirp });
  }

  // one grid step: the pattern decides what falls
  onStep(k, spb) {
    const p = this.p, dens = clamp(p.density, 0, 1), rate = Math.max(1, p.rate | 0), onBeat = k % rate === 0, r = Math.random;
    const cluster = (n, gap, size, kind) => { for (let j = 1; j < n; j++) this.pending.push({ at: Math.round(gap * j), size: size * (0.85 - j * 0.08), kind }); };
    switch (p.pattern) {
      case 'even': this.spawn(onBeat ? 0.85 : 0.6 + r() * 0.15, 'rain'); break;
      case 'rain': if (r() < 0.2 + dens * 0.7) { const s = r(); this.spawn(0.25 + s * s * 0.9, 'rain'); } break;
      case 'splash':
        if (onBeat || r() < 0.08 + dens * 0.45) { const n = 2 + ((r() * (2 + dens * 4)) | 0); this.spawn(0.9, 'splash'); cluster(n, spb / 4, 0.75, 'splash'); }
        break;
      case 'stream':
        this.spawn(0.35 + r() * 0.15, 'stream');
        if (r() < 0.15 + dens * 0.5) cluster(2 + ((r() * 3) | 0), spb / 3, 0.3, 'stream');
        break;
      case 'storm':
        if (onBeat) this.spawn(0.85, 'rain');
        if (r() < 0.3 + dens * 0.5) this.spawn(0.25 + r() * 0.6, 'rain');
        if (r() < 0.1 + dens * 0.3) cluster(3 + ((r() * 3) | 0), spb / 4, 0.6, 'splash');
        if (r() < 0.2 + dens * 0.4) cluster(2 + ((r() * 2) | 0), spb / 3, 0.3, 'stream');
        break;
      case '3d':
      case 'both':
      default:
        if (onBeat) this.spawn(0.9, 'rain');
        else if (r() < 0.15 + dens * 0.55) this.spawn(0.3 + r() * 0.5, 'rain');
    }
  }

  process(inputs, outputs) {
    const inp = inputs[0] || [], out = outputs[0], L = out[0], R = out[1] || out[0], n = L.length, sr = sampleRate, p = this.p;
    const inL = inp[0], inR = inp[1] || inp[0];
    const rate = Math.max(1, p.rate | 0), spb = sr * 60 / clamp(p.bpm, 30, 300) / rate;
    const dripRes = clamp(p.res, 0, 1), dripQ = 6 + dripRes * dripRes * 480, norm = 1 / Math.sqrt(1 + dripQ * 0.012);
    const ping = clamp(p.ping, 0, 1), sweep = clamp(p.sweep, -4, 4), fallT = clamp(p.fall, 0.02, 0.45);
    const bubble = clamp(p.bubble, 0, 1), wob = clamp(p.wobble, 0, 1), dguard = clamp(p.edge, 0, 1);
    const chirp = p.pattern === '3d' ? 1 : clamp(p.chirp, 0, 1);
    const mix = clamp(p.mix, 0, 1), dryAmt = mix < 0.5 ? 1 : 2 * (1 - mix), wetAmt = mix < 0.5 ? 2 * mix : 1;
    const chop = clamp(p.chop, 0, 1), sense = clamp(p.sense, 0, 1), drive = clamp(p.drive, 0, 4);
    const kFastA = 1 - Math.exp(-1 / (0.0005 * sr)), kFastR = 1 - Math.exp(-1 / (0.006 * sr));
    const kSlowA = 1 - Math.exp(-1 / (0.03 * sr)), kSlowR = 1 - Math.exp(-1 / (0.2 * sr));
    const kGateA = 1 - Math.exp(-1 / (0.001 * sr)), kGateR = 1 - Math.exp(-1 / (0.012 * sr));
    const kAtt = 1 / Math.max(1, 0.0008 * sr);
    const fType = p.fType, fOn = fType && fType !== 'off', fRes = clamp(p.fRes, 0, 1), fk = 2 - 1.96 * fRes, fk24 = 2 - 1.6 * fRes, bpGain = Math.pow(fk, 0.7) * 1.4;
    const fDrive = clamp(p.fDrive, 0, 1), fDg = 1 + fDrive * 5, fDn = 1 / (1 + fDrive * 1.6);
    const fAk = 1 - Math.exp(-1 / (Math.max(0.0008, p.fA) * sr)), fDk = 1 - Math.exp(-1 / (Math.max(0.005, p.fD) * sr));
    const lfoInc = clamp(p.fRate, 0.01, 20) / sr, fDepth = clamp(p.fDepth, 0, 1) * 3, fEnvOct = clamp(p.fEnv, -1, 1) * 5;
    let peak = 0;

    for (let i = 0; i < n; i++) {
      const xl = inL ? inL[i] : 0, xr = inR ? inR[i] : 0, mono = 0.5 * (xl + xr);
      // tempo grid
      this.clock += 1;
      if (this.clock >= spb) { this.clock -= spb; this.step += 1; this.onStep(this.step, spb); }
      for (let j = this.pending.length - 1; j >= 0; j--) { const q = this.pending[j]; if (--q.at <= 0) { this.spawn(q.size, q.kind); this.pending.splice(j, 1); } }
      // the input's own hits
      const a = Math.abs(mono);
      this.fast += (a - this.fast) * (a > this.fast ? kFastA : kFastR);
      this.slow += (a - this.slow) * (a > this.slow ? kSlowA : kSlowR);
      if (this.refractory > 0) this.refractory -= 1;
      else if (sense > 0.001 && this.fast > 0.02 && this.fast > this.slow * (1.25 + (1 - sense) * 3.5)) {
        this.spawn(clamp(this.fast * 2.2, 0.3, 1.2), 'note'); this.refractory = 0.06 * sr;
      }
      this.lfoPh += lfoInc; if (this.lfoPh >= 1) this.lfoPh -= 1;

      let wl = 0, wr = 0, gateNow = 0;
      for (const d of this.drops) {
        if (!d.active) continue;
        d.age += 1;
        d.env = d.age < 1 / kAtt ? d.age * kAtt : Math.exp(-(d.age - 1 / kAtt) / d.len);
        if (d.env < 0.006 && d.age > 1 / kAtt) { d.active = false; continue; }   // inaudible from here: free the voice
        if (d.env > gateNow) gateNow = d.env;
        let fall = Math.exp(-d.age / (fallT * sr * (0.45 + d.size * 0.9)));
        if (chirp > 0) fall *= Math.pow(fall, chirp);
        const scatter = d.jit * (0.25 + Math.abs(sweep) * 0.12);
        const swept = d.fc * Math.pow(2, sweep * fall + scatter * (0.45 + 0.55 * fall));
        const wobble = Math.sin(TAU * d.ph) * wob * (0.1 + 0.32 * d.size);
        const fc = clamp(swept * (1 + wobble), 32, sr * 0.45);
        d.ph = wrap01(d.ph + fc * (0.42 + dripRes * 0.5) * d.rate / sr);
        const g = Math.tan(Math.PI * fc / sr), k = 1 / dripQ, a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const gb = Math.tan(Math.PI * clamp(fc * (1.5 + d.ratio), 40, sr * 0.45) / sr), kb = 1 / Math.max(1.6, dripQ * 0.62);
        const b1 = 1 / (1 + gb * (gb + kb)), b2 = gb * b1, b3 = gb * b2;
        let exc = mono * drive * d.env * d.dGuard;
        if (d.age < 4 && ping > 0.001) exc += (d.age < 2 ? 0.8 : 0.18) * ping * d.size;
        const v3 = exc - d.ic2, v1 = a1 * d.ic1 + a2 * v3, v2 = d.ic2 + a2 * d.ic1 + a3 * v3;
        d.ic1 = 2 * v1 - d.ic1; d.ic2 = 2 * v2 - d.ic2;
        if (!Number.isFinite(d.ic1) || d.ic1 > 6 || d.ic1 < -6) { d.ic1 = 0; d.ic2 = 0; }
        const w3 = exc - d.ic2b, w1 = b1 * d.ic1b + b2 * w3, w2 = d.ic2b + b2 * d.ic1b + b3 * w3;
        d.ic1b = 2 * w1 - d.ic1b; d.ic2b = 2 * w2 - d.ic2b;
        if (!Number.isFinite(d.ic1b) || d.ic1b > 6 || d.ic1b < -6) { d.ic1b = 0; d.ic2b = 0; }
        let shaped = (v1 * 0.74 + v2 * 0.28 + w1 * bubble) * norm;
        // EDGE, as in the synth: off runs hot; up pulls back from overload; a blow-up is always cut
        const pk = Math.abs(shaped);
        d.dgLvl = pk > d.dgLvl ? pk : d.dgLvl * 0.9997;
        if (dguard > 0.001) {
          if (d.dgLvl > 2.6 - dguard * 1.4) d.dGuard = Math.max(0.4 - dguard * 0.2, d.dGuard * 0.997);
          else if (d.dGuard < 1) d.dGuard = Math.min(1, d.dGuard + 0.00002);
        } else d.dGuard = 1;
        if (d.dgLvl > 6) { d.ic1 *= 0.5; d.ic2 *= 0.5; d.ic1b *= 0.5; d.ic2b *= 0.5; d.dgLvl *= 0.5; this.cutFlash = 1; }
        if (d.dGuard !== 1) shaped *= d.dGuard;
        if (d.dgLvl > peak) peak = d.dgLvl;
        const y = Math.tanh(shaped * (0.5 + d.size * 0.7) * 1.35) * Math.min(1, d.env * 4);
        wl += y * d.gl; wr += y * d.gr;
      }
      // CHOP: the dry signal only passes while drips are falling
      this.gate += (gateNow - this.gate) * (gateNow > this.gate ? kGateA : kGateR);
      const dryGain = dryAmt * (1 - chop * (1 - Math.min(1, this.gate * 1.6)));
      let ol = xl * dryGain + wl * wetAmt * 0.7, or = xr * dryGain + wr * wetAmt * 0.7;
      if (fOn) {
        if (this.fstage === 1) { this.fenv += (1.02 - this.fenv) * fAk; if (this.fenv >= 1) { this.fenv = 1; this.fstage = 2; } }
        else this.fenv += (0 - this.fenv) * fDk;
        if ((i & 7) === 0) this.fg = Math.tan(Math.PI * clamp(p.fCut * Math.pow(2, fEnvOct * this.fenv + fDepth * Math.sin(this.lfoPh * TAU)), 20, sr * 0.45) / sr);
        if (fDrive > 0.001) { ol = Math.tanh(ol * fDg) * fDn; or = Math.tanh(or * fDg) * fDn; }
        const st = this.fst, g = this.fg, k = fType === 'lp24' ? fk24 : fk;
        const pick = (x, o) => {
          const [b, l] = svf(st, o, x, g, k);
          if (fType === 'lp') return l;
          if (fType === 'lp24') return svf(st, o + 4, l, g, k)[1];
          if (fType === 'hp') return x - k * b - l;
          if (fType === 'bp') return b * bpGain;
          return x - k * b;
        };
        ol = pick(ol, 0); or = pick(or, 2);
        if (!Number.isFinite(ol) || !Number.isFinite(or)) { st.fill(0); ol = or = 0; }
      }
      L[i] = ol * p.level; if (R !== L) R[i] = or * p.level;
    }
    this.peak = Math.max(this.peak * 0.9, peak);
    if (++this.reportCount * n >= this.reportEvery) {
      this.reportCount = 0;
      this.port.postMessage({ type: 'frame', events: this.events, peak: this.peak, cut: this.cutFlash, voices: this.drops.filter((d) => d.active).length, step: this.step });
      this.events = []; this.cutFlash = 0;
    }
    return true;
  }
}
registerProcessor('fluidic-fx', FluidicFxProcessor);
