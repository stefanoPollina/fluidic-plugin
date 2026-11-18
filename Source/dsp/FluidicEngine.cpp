// FLUIDIC — drip synthesis engine. Port of fluidic-worklet.js; keep the two in step.
// Comments mark the few places where C++ needs care to match JavaScript exactly
// (integer truncation, float32 buffers, order of random draws).
#include "FluidicEngine.h"
#include <algorithm>
#include <cmath>

namespace fluidic
{
namespace
{
constexpr double PI = 3.14159265358979323846;
constexpr double TAU = PI * 2;
inline double clampd (double x, double a, double b) { return x < a ? a : x > b ? b : x; }
inline double wrap01 (double x) { x -= std::floor (x); return x < 0 ? x + 1 : x; }
inline bool finite (double x) { return std::isfinite (x); }

// zero-delay-feedback state-variable filter step; returns {band, low}
inline void svf (std::array<double, 8>& st, int o, double x, double g, double k, double& band, double& low)
{
    const double a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
    const double v3 = x - st[(size_t) o + 1], v1 = a1 * st[(size_t) o] + a2 * v3, v2 = st[(size_t) o + 1] + a2 * st[(size_t) o] + a3 * v3;
    st[(size_t) o] = 2 * v1 - st[(size_t) o]; st[(size_t) o + 1] = 2 * v2 - st[(size_t) o + 1];
    band = v1; low = v2;
}
} // namespace

void FluidicEngine::prepare (double sampleRate)
{
    sr = sampleRate;
    stealK = 1 - std::exp (-1 / (0.0006 * sr));   // ~2 ms fade when a sounding voice is reused
    events.reserve (128);                          // drop() caps at 65, so the audio thread never allocates
    panic();
}

int FluidicEngine::activeVoices() const
{
    int n = 0; for (auto& v : voices) n += v.active ? 1 : 0; return n;
}

void FluidicEngine::panic()
{
    for (auto& v : voices) { v.active = false; v.stage = Stage::idle; v.env = 0; }
}

void FluidicEngine::noteOn (int note, double vel, bool noSteal)
{
    Voice* v = nullptr;
    for (auto& x : voices) if (! x.active) { v = &x; break; }
    if (v == nullptr)
    {
        if (noSteal) return;   // ornaments (e.g. Foldspace bubbles) only use free voices
        // JS: voices.reduce((a, b) => (a.env < b.env ? a : b)) — on a tie the later voice wins
        v = &voices[0];
        for (size_t i = 1; i < voices.size(); ++i) if (! (v->env < voices[i].env)) v = &voices[i];
        v->pendNote = note; v->pendVel = vel; v->pendOff = false; v->stage = Stage::steal; v->gate = false; v->id = -1;
        return;
    }
    startVoice (*v, note, vel);
}

void FluidicEngine::startVoice (Voice& v, int note, double vel)
{
    const bool wasIdle = ! v.active;
    v.active = true; v.id = note; v.note = note; v.vel = vel; v.gate = true; v.age = 0; v.stage = Stage::attack;
    v.dripKick = true; v.dripAge = 1e9; v.dGuard = 1; v.dgLvl = 0;
    v.ic1 = v.ic2 = v.ic1b = v.ic2b = 0;
    v.fenv = 0; v.fstage = 1; if (wasIdle) v.fst.fill (0);
    double s = 0; for (double x : v.fst) s += x;
    if (! finite (s)) v.fst.fill (0);
    v.freq = 440 * std::pow (2.0, (note + 12 * p.octave - 69) / 12);
    for (auto& x : v.noise) x = (float) (rng.next() * 2 - 1);   // HOLD: a random cycle looped at the note's pitch
    v.pan = clampd ((note - 60) / 36.0, -0.6, 0.6);
    v.fc = clampd (p.cutoff * std::pow (2.0, (note - 60) / 12.0 * p.key), 30, sr * 0.45);
    const double angle = (v.pan * 0.6 + 1) * 0.25 * PI; v.gl = std::cos (angle); v.gr = std::sin (angle);
}

void FluidicEngine::noteOff (int note)
{
    for (auto& v : voices)
    {
        if (v.active && v.id == note && v.gate) { v.gate = false; v.stage = Stage::release; }
        else if (v.stage == Stage::steal && v.pendNote == note) v.pendOff = true;
    }
}

void FluidicEngine::drop (const Voice& v, double size, DropKind kind)
{
    if (events.size() > 64) return;
    const double spread = 0.12 + p.width * 0.38;
    events.push_back ({ (float) clampd (0.5 + v.pan * 0.45 + v.dripJit * spread, 0.03, 0.97), (float) size, kind, v.note, (float) p.sweep, (float) p.chirp });
}

double FluidicEngine::env (Voice& v, double ka, double kd, double kr)
{
    switch (v.stage)
    {
        case Stage::attack:  v.env += (1.02 - v.env) * ka; if (v.env >= 1) { v.env = 1; v.stage = Stage::decay; } break;
        case Stage::decay:   v.env += (p.s - v.env) * kd; break;
        case Stage::release: v.env += (0 - v.env) * kr; if (v.env < 0.0004) { v.env = 0; v.stage = Stage::idle; v.active = false; } break;
        case Stage::steal:
            v.env -= v.env * stealK;
            if (v.env < 0.001)
            {
                const bool off = v.pendOff; v.env = 0; v.active = false; startVoice (v, v.pendNote, v.pendVel); v.pendNote = -1;
                if (off) { v.gate = false; v.stage = Stage::release; }
            }
            break;
        case Stage::idle: break;
    }
    return v.env;
}

double FluidicEngine::osc (Voice& v)
{
    const double ph = v.phase, dt = v.freq / sr;
    v.phase = wrap01 (ph + dt);
    double y;
    switch (p.wave)
    {
        case Wave::sine: y = std::sin (TAU * ph); break;
        case Wave::square: y = ph < 0.5 ? 1 : -1; break;
        case Wave::noise:
        {
            const double wn = rng.next() * 2 - 1, hold = clampd (p.hold, 0, 1);
            y = wn * (1 - hold) + (double) v.noise[(size_t) (int) (ph * 64)] * hold; break;
        }
        case Wave::saw:
        default:
        {   // saw with a light polyBLEP
            y = 2 * ph - 1;
            if (ph < dt) { const double t = ph / dt; y -= t + t - t * t - 1; }
            else if (ph > 1 - dt) { const double t = (ph - 1) / dt; y -= t * t + t + t + 1; }
        }
    }
    return y * p.oscLevel;
}

void FluidicEngine::process (float* outL, float* outR, int n)
{
    // the worklet runs in 128-sample blocks; per-block constants are recomputed at the same points
    while (n > 0)
    {
        const int b = std::min (n, 128);
        renderBlock (outL, outR, b);
        outL += b; outR += b; n -= b;
    }
}

void FluidicEngine::renderBlock (float* L, float* R, int n)
{
    const Pattern rawMode = p.pattern;
    enum { mEven, mRain, mBoth } dripMode;
    enum { sNone, sSplash, sStream } dripStyle = rawMode == Pattern::splash ? sSplash : rawMode == Pattern::stream ? sStream : sNone;
    dripMode = rawMode == Pattern::even ? mEven : rawMode == Pattern::rain ? mRain : dripStyle != sNone ? mRain : mBoth;
    const bool storm = rawMode == Pattern::storm;
    const double dripRes = clampd (p.res, 0, 1), dripQ = 6 + dripRes * dripRes * 480;
    const double ping = clampd (p.ping, 0, 1), sweep = clampd (p.sweep, -4, 4), fallT = clampd (p.fall, 0.02, 0.45);
    const double bubble = clampd (p.bubble, 0, 1), wob = clampd (p.wobble, 0, 1);
    double chirp = clampd (p.chirp, 0, 1), dwidth = clampd (p.width, 0, 1);
    const double density = clampd (p.density, 0, 1), dsize = clampd (p.size, 0, 1), dguard = clampd (p.edge, 0, 1);
    if (rawMode == Pattern::threeD) { chirp = 1; if (dwidth < 0.8) dwidth = 0.8; }
    const double dens = density == 0.5 ? 1 : std::pow (2.0, (0.5 - density) * 4);
    const double norm = 1 / std::sqrt (1 + dripQ * 0.012);
    double blockPeak = 0;
    const FilterType fType = p.fType; const bool fOn = fType != FilterType::off;
    const double fRes = clampd (p.fRes, 0, 1), fk = 2 - 1.96 * fRes, fk24 = 2 - 1.6 * fRes, bpGain = std::pow (fk, 0.7) * 1.4;
    const double fDrive = clampd (p.fDrive, 0, 1), fDg = 1 + fDrive * 5, fDn = 1 / (1 + fDrive * 1.6);
    const double fAk = 1 - std::exp (-1 / (std::max (0.0008, p.fA) * sr)), fDk = 1 - std::exp (-1 / (std::max (0.005, p.fD) * sr));
    const double lfoInc = clampd (p.fRate, 0.01, 20) / sr, fDepth = clampd (p.fDepth, 0, 1) * 3, fEnvOct = clampd (p.fEnv, -1, 1) * 5;
    auto ek = [this] (double t) { return 1 - std::exp (-1 / (std::max (0.0008, t) * sr)); };
    const double ka = ek (p.a), kd = ek (p.d), kr = ek (p.r);
    for (auto& v : voices)
    {
        if (! v.active) continue;
        v.fc = clampd (p.cutoff * std::pow (2.0, (v.note - 60) / 12.0 * p.key), 30, sr * 0.45);
        const double angle = (v.pan * 0.6 + 1) * 0.25 * PI; v.gl = std::cos (angle); v.gr = std::sin (angle);
    }

    for (int i = 0; i < n; ++i)
    {
        double busL = 0, busR = 0;
        lfoPh += lfoInc; if (lfoPh >= 1) lfoPh -= 1;
        const double lfo = std::sin (lfoPh * TAU);
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const double envv = env (v, ka, kd, kr);
            if (! v.active) continue;
            v.age += 1;
            const double mix = osc (v);

            if (v.dripKick)
            {
                v.dripKick = false;
                if (dripMode != mEven)
                {
                    v.dripWait = 1; v.splashN = 3 + (int) (rng.next() * 4);
                    if (storm) { v.stW = 1 + rng.next() * fallT * sr; v.spW = fallT * sr * (0.5 + rng.next() * 2); v.dripKind = 0.55; }
                }
                if (dripMode != mRain) { v.dripT0 = v.age; v.dripJit = (rng.next() * 2 - 1) * 0.3; drop (v, 1, DropKind::note); }
            }
            const double dAge = v.age - v.dripT0;
            if (storm && v.gate)
            {
                v.dripWait -= 1; v.stW -= 1; v.spW -= 1;
                int kind = 0;
                if (v.dripWait <= 0) { kind = 1; const double r = rng.next(); v.dripWait = std::max (12.0, fallT * sr * (0.28 + r * r * 7.4) * dens * 1.6); }
                else if (v.spW <= 0)
                {
                    kind = 2; const double r = rng.next();
                    if (v.splashN > 0) { v.splashN -= 1; v.spW = std::max (12.0, fallT * sr * (0.1 + r * 0.28) * dens); }
                    else { v.splashN = 2 + (int) (rng.next() * 4); v.spW = std::max (12.0, fallT * sr * (3 + r * 10) * dens); }
                }
                else if (v.stW <= 0) { kind = 3; const double r = rng.next(); v.stW = std::max (12.0, fallT * sr * (0.1 + r * 0.4) * dens * 1.5); }
                if (kind)
                {
                    v.dripAge = 0; v.dripHit = 3;
                    const double sr2 = rng.next();
                    v.dripSize = (0.12 + sr2 * sr2 * 0.88) * (kind == 3 ? 0.4 : kind == 2 ? 0.8 : 1);
                    if (dsize != 0.5) v.dripSize = clampd (v.dripSize * std::pow (2.0, (dsize - 0.5) * 2.4), 0.04, 1.5);
                    v.dripJit = rng.next() * 2 - 1; v.dripRatio = rng.next() * 0.7 - 0.22;
                    v.dripAmp = rng.next() * 2 - 1; v.dripRate = 0.25 + rng.next() * 0.9;
                    v.dripKind = kind == 1 ? 0.55 : kind == 2 ? 0.45 : 0.28;
                    drop (v, v.dripSize, kind == 1 ? DropKind::rain : kind == 2 ? DropKind::splash : DropKind::stream);
                }
            }
            if (dripMode != mEven && v.gate && ! storm)
            {
                v.dripWait -= 1;
                if (v.dripWait <= 0)
                {
                    const double g = rng.next();
                    double gap = fallT * sr * (0.28 + g * g * 7.4) * dens;
                    if (dripStyle == sStream) gap = fallT * sr * (0.06 + g * 0.24) * dens;
                    else if (dripStyle == sSplash)
                    {
                        if (v.splashN > 0) { v.splashN -= 1; gap = fallT * sr * (0.1 + g * 0.28) * dens; }
                        else { v.splashN = 2 + (int) (rng.next() * 5); gap = fallT * sr * (2.5 + g * 9) * dens; }
                    }
                    v.dripWait = gap < 12 ? 12 : gap;
                    v.dripAge = 0; v.dripHit = 3;
                    const double sr2 = rng.next();
                    v.dripSize = 0.12 + sr2 * sr2 * 0.88;
                    if (dsize != 0.5) v.dripSize = clampd (v.dripSize * std::pow (2.0, (dsize - 0.5) * 2.4), 0.04, 1.5);
                    if (dripStyle == sStream) v.dripSize *= 0.45;
                    v.dripJit = rng.next() * 2 - 1; v.dripRatio = rng.next() * 0.7 - 0.22;
                    v.dripAmp = rng.next() * 2 - 1; v.dripRate = 0.25 + rng.next() * 0.9;
                    drop (v, v.dripSize, dripStyle == sStream ? DropKind::stream : dripStyle == sSplash ? DropKind::splash : DropKind::rain);
                }
            }
            if (v.dripHit > 0) v.dripHit -= 1;
            v.dripAge += 1;

            double fall, swept, wobble, dripRatio;
            if (dripMode == mRain)
            {
                fall = std::exp (-v.dripAge / (fallT * sr * (0.45 + v.dripSize * 0.9)));
                if (chirp > 0) fall *= std::pow (fall, chirp);
                const double scatter = v.dripJit * (0.35 + std::abs (sweep) * 0.2);
                swept = v.fc * std::pow (2.0, sweep * fall + scatter * (0.45 + 0.55 * fall));
                wobble = std::sin (TAU * v.dripPh) * wob * (0.1 + 0.32 * v.dripSize);
                dripRatio = 1.28 + v.dripRatio + 0.22;
            }
            else if (dripMode == mBoth)
            {
                fall = std::exp (-dAge / (fallT * sr));
                double dropFall = std::exp (-v.dripAge / (fallT * sr * (0.5 + v.dripSize)));
                if (chirp > 0) { fall *= std::pow (fall, chirp); dropFall *= std::pow (dropFall, chirp); }
                const double scatter = v.dripJit * dropFall * (0.22 + std::abs (sweep) * 0.12);
                swept = v.fc * std::pow (2.0, sweep * fall + scatter);
                wobble = std::sin (TAU * v.dripPh) * wob * 0.28 * (1 + 0.55 * v.dripAmp * dropFall);
                dripRatio = 1.51 + v.dripRatio * dropFall * 0.45;
            }
            else
            {
                fall = std::exp (-dAge / (fallT * sr));
                if (chirp > 0) fall *= std::pow (fall, chirp);
                swept = v.fc * std::pow (2.0, sweep * fall);
                wobble = std::sin (TAU * v.dripPh) * wob * 0.28;
                dripRatio = 1.51;
            }
            const double fc = clampd (swept * (1 + wobble), 32, sr * 0.45);
            v.dripPh = wrap01 (v.dripPh + (fc * (0.42 + dripRes * 0.5) * (dripMode == mRain ? v.dripRate : 1)) / sr);
            const double g = std::tan (PI * fc / sr), k = 1 / dripQ, a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const double gb = std::tan (PI * clampd (fc * dripRatio, 40, sr * 0.45) / sr), kb = 1 / std::max (1.6, dripQ * 0.62);
            const double b1 = 1 / (1 + gb * (gb + kb)), b2 = gb * b1, b3 = gb * b2;
            double exc = mix;
            if (dripMode != mRain && dAge < 5 && ping > 0.001) exc += (dAge < 2 ? 0.9 : 0.16) * ping * (storm ? 0.6 : 1);
            if (dripMode != mEven && v.dripHit > 0 && ping > 0.001) exc += (v.dripHit > 1 ? 0.8 : 0.18) * ping * v.dripSize * (storm ? v.dripKind : 1) * v.dGuard;
            const double v3 = exc - v.ic2, v1 = a1 * v.ic1 + a2 * v3, v2 = v.ic2 + a2 * v.ic1 + a3 * v3;
            v.ic1 = 2 * v1 - v.ic1; v.ic2 = 2 * v2 - v.ic2;
            if (! finite (v.ic1) || v.ic1 > 6 || v.ic1 < -6) { v.ic1 = 0; v.ic2 = 0; }
            const double w3 = exc - v.ic2b, w1 = b1 * v.ic1b + b2 * w3, w2 = v.ic2b + b2 * v.ic1b + b3 * w3;
            v.ic1b = 2 * w1 - v.ic1b; v.ic2b = 2 * w2 - v.ic2b;
            if (! finite (v.ic1b) || v.ic1b > 6 || v.ic1b < -6) { v.ic1b = 0; v.ic2b = 0; }
            double shaped = (v1 * 0.74 + v2 * 0.28 + w1 * bubble) * norm;
            // EDGE: off = runs as hot as it goes; turning it up pulls back from overload. A real blow-up is always cut.
            const double pk = std::abs (shaped);
            v.dgLvl = pk > v.dgLvl ? pk : v.dgLvl * 0.9997;
            if (dguard > 0.001)
            {
                if (v.dgLvl > 2.6 - dguard * 1.4) v.dGuard = std::max (0.4 - dguard * 0.2, v.dGuard * 0.997);
                else if (v.dGuard < 1) v.dGuard = std::min (1.0, v.dGuard + 0.00002);
            }
            else v.dGuard = 1;
            if (v.dgLvl > 6) { v.ic1 *= 0.5; v.ic2 *= 0.5; v.ic1b *= 0.5; v.ic2b *= 0.5; v.dgLvl *= 0.5; cutFlash = true; }
            if (v.dGuard != 1) shaped *= v.dGuard;
            if (v.dgLvl > blockPeak) blockPeak = v.dgLvl;
            double sl = shaped, srr = shaped;
            if (dwidth > 0.001)
            {
                const double pri = (v1 * 0.74 + v2 * 0.28) * norm, sec = w1 * bubble * norm;
                auto& dl = v.dripDelay;
                dl[(size_t) v.dripDPos] = (float) sec;   // Float32Array in the worklet
                const int tap = (v.dripDPos - (2 + (int) (dwidth * (6 + bubble * 14 + std::abs (v.dripJit) * 5))) + 32) & 31;
                v.dripDPos = (v.dripDPos + 1) & 31;
                sl = (pri + sec * 0.9) * v.dGuard; srr = (pri + (sec * (1 - dwidth) + (double) dl[(size_t) tap] * dwidth) * 0.9) * v.dGuard;
                if (! finite (sl) || ! finite (srr)) { dl.fill (0); sl = srr = 0; }
            }
            if (fOn)
            {
                // filter envelope: attack up, decay back to the dial's cutoff
                if (v.fstage == 1) { v.fenv += (1.02 - v.fenv) * fAk; if (v.fenv >= 1) { v.fenv = 1; v.fstage = 2; } }
                else v.fenv += (0 - v.fenv) * fDk;
                if ((i & 7) == 0)
                {
                    const double oct = p.fKey * (v.note - 60) / 12 + fEnvOct * v.fenv + fDepth * lfo;
                    const double fcut = clampd (p.fCut * std::pow (2.0, oct), 20, sr * 0.45);
                    v.fg = std::tan (PI * fcut / sr);
                }
                double xl = sl, xr = srr;
                if (fDrive > 0.001) { xl = std::tanh (xl * fDg) * fDn; xr = std::tanh (xr * fDg) * fDn; }
                const double gg = v.fg, kk = fType == FilterType::lp24 ? fk24 : fk;
                auto pick = [&] (double x, int o)
                {
                    double band, low; svf (v.fst, o, x, gg, kk, band, low);
                    switch (fType)
                    {
                        case FilterType::lp: return low;
                        case FilterType::lp24: { double b2b, l2; svf (v.fst, o + 4, low, gg, kk, b2b, l2); return l2; }
                        case FilterType::hp: return x - kk * band - low;
                        case FilterType::bp: return band * bpGain;
                        default: return x - kk * band;   // notch
                    }
                };
                sl = pick (xl, 0); srr = pick (xr, 2);
                if (! finite (sl) || ! finite (srr)) { v.fst.fill (0); sl = srr = 0; }
            }
            const double amp = envv * (0.3 + 0.7 * v.vel) * p.level;
            busL += std::tanh (sl * amp * 1.35) * v.gl;
            busR += std::tanh (srr * amp * 1.35) * v.gr;
        }
        L[i] = (float) (busL * 0.55); R[i] = (float) (busR * 0.55);
    }
    peak = std::max (peak * 0.9, blockPeak);
}
} // namespace fluidic
