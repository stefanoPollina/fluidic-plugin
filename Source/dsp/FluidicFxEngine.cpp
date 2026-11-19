// FLUIDIC FX engine. Port of fluidic-fx-worklet.js; keep the two in step (tests/compare_fx.py checks them).
// Random draws happen in exactly the same order as in JavaScript, so a seeded render matches sample for sample.
#include "FluidicFxEngine.h"
#include <algorithm>
#include <cmath>

namespace fluidic
{
namespace
{
constexpr double PI = 3.14159265358979323846, TAU = PI * 2;
inline double clampd (double x, double a, double b) { return x < a ? a : x > b ? b : x; }
inline double wrap01 (double x) { x -= std::floor (x); return x < 0 ? x + 1 : x; }
inline void svf (std::array<double, 8>& st, int o, double x, double g, double k, double& band, double& low)
{
    const double a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
    const double v3 = x - st[(size_t) o + 1], v1 = a1 * st[(size_t) o] + a2 * v3, v2 = st[(size_t) o + 1] + a2 * st[(size_t) o] + a3 * v3;
    st[(size_t) o] = 2 * v1 - st[(size_t) o]; st[(size_t) o + 1] = 2 * v2 - st[(size_t) o + 1];
    band = v1; low = v2;
}
}

void FluidicFxEngine::prepare (double sampleRate)
{
    sr = sampleRate; events.reserve (128); pending.reserve (64); held.reserve (16);
    panic(); sync(); fast = slow = lvl = refractory = gate = 0; fst.fill (0); fenv = 0; fstage = 0;
}

int FluidicFxEngine::activeDrops() const { int n = 0; for (auto& d : drops) n += d.active ? 1 : 0; return n; }

void FluidicFxEngine::panic() { held.clear(); pending.clear(); for (auto& d : drops) d.active = false; }

void FluidicFxEngine::noteOn (int note)
{
    held.erase (std::remove (held.begin(), held.end(), note), held.end()); held.push_back (note);
    spawn (0.95, DropKind::note, note);
}
void FluidicFxEngine::noteOff (int note) { held.erase (std::remove (held.begin(), held.end(), note), held.end()); }

void FluidicFxEngine::setGridPosition (double stepsFromStart)
{
    // called each block while the host is playing: keep our grid on the host's
    gridRun = true;
    const long long k = (long long) std::floor (stepsFromStart + 1e-9);
    const double rate = std::max (1, p.rate), spb = sr * 60 / clampd (p.bpm, 30, 300) / rate;
    const double frac = stepsFromStart - (double) k;
    if (gridFresh)   // playback just started: if it starts on a step (e.g. bar 1), that step's drip falls now
    {
        gridFresh = false;
        if (frac * spb < 1.0) { step = k - 1; clock = spb - 1e-6; return; }
        step = k; clock = frac * spb; return;
    }
    if (k != step || std::abs (clock - frac * spb) > 2)
    {
        if (k != step && frac * spb < 1.0) { step = k - 1; clock = spb - 1e-6; }   // a step falls right here: let the grid fire it
        else { step = k; clock = frac * spb; }
    }
}

void FluidicFxEngine::spawn (double size, DropKind kind, int note, bool c3)
{
    Drop* d = nullptr;
    for (auto& x : drops) if (! x.active) { d = &x; break; }
    if (d == nullptr) { d = &drops[0]; for (size_t i = 1; i < drops.size(); ++i) if (! (d->env < drops[i].env)) d = &drops[i]; }
    const int n = note >= 0 ? note : (! held.empty() ? held.back() : 60);
    const double dsize = clampd (p.size, 0, 1);
    size = clampd (size * std::pow (2.0, (dsize - 0.5) * 2.4), 0.05, 1.5);
    d->active = true; d->age = 0; d->size = size;
    d->jit = rng.next() * 2 - 1; d->ratio = rng.next() * 0.7 - 0.22; d->amp = rng.next() * 2 - 1; d->rate = 0.25 + rng.next() * 0.9;
    d->ic1 = d->ic2 = d->ic1b = d->ic2b = 0; d->dGuard = 1; d->dgLvl = 0; d->env = 0;
    d->fc = clampd (p.cutoff * std::pow (2.0, (n - 60) / 12.0 * clampd (p.key, 0, 1)), 30, sr * 0.45);
    double width = clampd (p.width, 0, 1); if ((p.pattern == Pattern::threeD || c3) && width < 0.8) width = 0.8;
    d->c3 = c3;
    const double angle = (clampd (d->jit * width, -1, 1) + 1) * 0.25 * PI; d->gl = std::cos (angle); d->gr = std::sin (angle);
    d->len = clampd (p.fall, 0.02, 0.45) * sr * (1.5 + size * 4);
    // tone layer: this drip's own oscillator, at the drip's note
    d->note = n; d->freq = 440 * std::pow (2.0, (n + 12 * (int) p.octave - 69) / 12.0);
    for (auto& x : d->noise) x = (float) (rng.next() * 2 - 1);
    // AMP: attack → decay to sustain for one grid step, then release
    d->aenv = 0; d->astage = 1; d->gate = std::max (1.0, sr * 60 / clampd (p.bpm, 30, 300) / std::max (1, p.rate));
    lastNote = n;
    fenv = 0; fstage = 1;
    if (events.size() < 64) events.push_back ({ (float) clampd (0.5 + d->jit * width * 0.45, 0.03, 0.97), (float) size, kind, n, (float) p.sweep, (float) p.chirp });
}

void FluidicFxEngine::onStep (long long k, double spb)
{
    const double dens = clampd (p.density, 0, 1); const int rate = std::max (1, p.rate); const bool onBeat = k % rate == 0;
    auto r = [this] { return rng.next(); };
    // FOLLOW: drips only fall while the input has sound (-70 dB shut … -45 dB fully open)
    const double follow = clampd (p.follow, 0, 1);
    if (follow > 0.001)
    {
        const double open = clampd ((20 * std::log10 (lvl + 1e-9) + 70) / 25, 0, 1), keep = 1 - follow * (1 - open);
        if (keep < 1 && r() >= keep) return;
    }
    auto cluster = [this] (int n, double gap, double size, DropKind kind)
    { for (int j = 1; j < n; ++j) pending.push_back ({ (double) std::llround (gap * j), size * (0.85 - j * 0.08), kind }); };
    // STEP SEQUENCER: lit steps always drip, as their own pattern, as hard as their accent; unlit steps are silent
    if (seq.on)
    {
        const int len = 4 * rate; const auto& st = seq.steps[(size_t) (((k % len) + len) % len)];
        if (! st.on) return;
        const double A = 0.25 + clampd (st.acc, 0, 1);
        switch (st.pat >= 0 ? (Pattern) st.pat : p.pattern)
        {
            case Pattern::rain: { const double s = r(); spawn ((0.25 + s * s * 0.9) * A, DropKind::rain); break; }
            case Pattern::splash: { const int n = 2 + (int) (r() * (2 + dens * 4)); spawn (0.9 * A, DropKind::splash); cluster (n, spb / 4, 0.75 * A, DropKind::splash); break; }
            case Pattern::stream: { const double s = 0.35 + r() * 0.15; spawn (s * A, DropKind::stream); const int n = 2 + (int) (r() * 3); cluster (n, spb / 3, 0.3 * A, DropKind::stream); break; }
            case Pattern::storm: { spawn (0.85 * A, DropKind::rain); const int n = 3 + (int) (r() * 3); cluster (n, spb / 4, 0.6 * A, DropKind::splash); break; }
            case Pattern::threeD: spawn (0.9 * A, DropKind::rain, -1, true); break;
            default: spawn (0.85 * A, DropKind::rain);   // even, both
        }
        return;
    }
    switch (p.pattern)
    {
        case Pattern::even: { const double s = onBeat ? 0.85 : 0.6 + r() * 0.15; spawn (s, DropKind::rain); break; }
        case Pattern::rain: if (r() < 0.2 + dens * 0.7) { const double s = r(); spawn (0.25 + s * s * 0.9, DropKind::rain); } break;
        case Pattern::splash:
            if (onBeat || r() < 0.08 + dens * 0.45) { const int n = 2 + (int) (r() * (2 + dens * 4)); spawn (0.9, DropKind::splash); cluster (n, spb / 4, 0.75, DropKind::splash); }
            break;
        case Pattern::stream:
        {
            const double s = 0.35 + r() * 0.15; spawn (s, DropKind::stream);
            if (r() < 0.15 + dens * 0.5) { const int n = 2 + (int) (r() * 3); cluster (n, spb / 3, 0.3, DropKind::stream); }
            break;
        }
        case Pattern::storm:
            if (onBeat) spawn (0.85, DropKind::rain);
            if (r() < 0.3 + dens * 0.5) { const double s = 0.25 + r() * 0.6; spawn (s, DropKind::rain); }
            if (r() < 0.1 + dens * 0.3) { const int n = 3 + (int) (r() * 3); cluster (n, spb / 4, 0.6, DropKind::splash); }
            if (r() < 0.2 + dens * 0.4) { const int n = 2 + (int) (r() * 2); cluster (n, spb / 3, 0.3, DropKind::stream); }
            break;
        case Pattern::threeD:
        case Pattern::both:
        default:
            if (onBeat) spawn (0.9, DropKind::rain);
            else if (r() < 0.15 + dens * 0.55) { const double s = 0.3 + r() * 0.5; spawn (s, DropKind::rain); }
    }
}

void FluidicFxEngine::process (const float* inL, const float* inR, float* outL, float* outR, int n)
{
    while (n > 0)
    {
        const int b = std::min (n, 128);
        renderBlock (inL, inR, outL, outR, b);
        if (inL != nullptr) inL += b;
        if (inR != nullptr) inR += b;
        outL += b; outR += b; n -= b;
    }
}

void FluidicFxEngine::renderBlock (const float* inL, const float* inR, float* L, float* R, int n)
{
    const int rate = std::max (1, p.rate); const double spb = sr * 60 / clampd (p.bpm, 30, 300) / rate;
    const double dripRes = clampd (p.res, 0, 1), dripQ = 6 + dripRes * dripRes * 480, norm = 1 / std::sqrt (1 + dripQ * 0.012);
    const double ping = clampd (p.ping, 0, 1), sweep = clampd (p.sweep, -4, 4), fallT = clampd (p.fall, 0.02, 0.45);
    const double bubble = clampd (p.bubble, 0, 1), wob = clampd (p.wobble, 0, 1), dguard = clampd (p.edge, 0, 1);
    const double chirp = p.pattern == Pattern::threeD ? 1 : clampd (p.chirp, 0, 1);
    const double mix = clampd (p.mix, 0, 1), dryAmt = mix < 0.5 ? 1 : 2 * (1 - mix), wetAmt = mix < 0.5 ? 2 * mix : 1;
    const double chop = clampd (p.chop, 0, 1), sense = clampd (p.sense, 0, 1), drive = clampd (p.drive, 0, 4);
    const double kFastA = 1 - std::exp (-1 / (0.0005 * sr)), kFastR = 1 - std::exp (-1 / (0.006 * sr));
    const double kSlowA = 1 - std::exp (-1 / (0.03 * sr)), kSlowR = 1 - std::exp (-1 / (0.2 * sr)), kLvlR = 1 - std::exp (-1 / (0.15 * sr));
    const double kGateA = 1 - std::exp (-1 / (0.001 * sr)), kGateR = 1 - std::exp (-1 / (0.012 * sr));
    const double kAtt = 1 / std::max (1.0, 0.0008 * sr), attLen = 1 / kAtt;
    const FilterType fType = p.fType; const bool fOn = fType != FilterType::off;
    const double fRes = clampd (p.fRes, 0, 1), fk = 2 - 1.96 * fRes, fk24 = 2 - 1.6 * fRes, bpGain = std::pow (fk, 0.7) * 1.4;
    const double fDrive = clampd (p.fDrive, 0, 1), fDg = 1 + fDrive * 5, fDn = 1 / (1 + fDrive * 1.6);
    const double fAk = 1 - std::exp (-1 / (std::max (0.0008, p.fA) * sr)), fDk = 1 - std::exp (-1 / (std::max (0.005, p.fD) * sr));
    const double lfoInc = clampd (p.fRate, 0.01, 20) / sr, fDepth = clampd (p.fDepth, 0, 1) * 3, fEnvOct = clampd (p.fEnv, -1, 1) * 5;
    auto ek = [this] (double t) { return 1 - std::exp (-1 / (std::max (0.0008, t) * sr)); };
    const double ka = ek (p.a), kd = ek (p.d), kr = ek (p.r), sus = clampd (p.s, 0, 1);
    const double oscLevel = clampd (p.oscLevel, 0, 1), hold = clampd (p.hold, 0, 1); const Wave wave = p.wave;
    const double keyOct = clampd (p.fKey, 0, 1) * ((! held.empty() ? held.back() : lastNote) - 60) / 12.0;
    double blockPeak = 0;

    for (int i = 0; i < n; ++i)
    {
        const double xl = inL ? inL[i] : 0, xr = inR ? inR[i] : (inL ? inL[i] : 0), mono = 0.5 * (xl + xr);
        if (gridRun)
        {
            clock += 1;
            if (clock >= spb) { clock -= spb; step += 1; onStep (step, spb); }
        }
        for (int j = (int) pending.size() - 1; j >= 0; --j)
        {
            auto& q = pending[(size_t) j];
            if (--q.at <= 0) { const Pending pq = q; pending.erase (pending.begin() + j); spawn (pq.size, pq.kind); }
        }
        const double a = std::abs (mono);
        fast += (a - fast) * (a > fast ? kFastA : kFastR);
        slow += (a - slow) * (a > slow ? kSlowA : kSlowR);
        lvl += (a - lvl) * (a > lvl ? kSlowA : kLvlR);
        if (refractory > 0) refractory -= 1;
        else if (sense > 0.001 && fast > 0.02 && fast > slow * (1.25 + (1 - sense) * 3.5)) { spawn (clampd (fast * 2.2, 0.3, 1.2), DropKind::note); refractory = 0.06 * sr; }
        lfoPh += lfoInc; if (lfoPh >= 1) lfoPh -= 1;

        double wl = 0, wr = 0, gateNow = 0;
        for (auto& d : drops)
        {
            if (! d.active) continue;
            d.age += 1;
            // AMP: one grid step is the drip's note length
            if (d.astage == 1) { d.aenv += (1.02 - d.aenv) * ka; if (d.aenv >= 1) { d.aenv = 1; d.astage = 2; } }
            else if (d.astage == 2) d.aenv += (sus - d.aenv) * kd;
            else d.aenv += (0 - d.aenv) * kr;
            if (d.astage != 3 && d.age >= d.gate) d.astage = 3;
            const double natural = d.age < attLen ? d.age * kAtt : std::exp (-(d.age - attLen) / d.len);
            if ((natural < 0.006 && d.age > attLen) || (d.astage == 3 && d.aenv < 0.0004)) { d.active = false; continue; }
            d.env = natural * std::min (1.0, d.aenv);
            if (d.env > gateNow) gateNow = d.env;
            double fall = std::exp (-d.age / (fallT * sr * (0.45 + d.size * 0.9)));
            const double ch = d.c3 ? 1 : chirp;
            if (ch > 0) fall *= std::pow (fall, ch);
            const double scatter = d.jit * (0.25 + std::abs (sweep) * 0.12);
            const double swept = d.fc * std::pow (2.0, sweep * fall + scatter * (0.45 + 0.55 * fall));
            const double wobble = std::sin (TAU * d.ph) * wob * (0.1 + 0.32 * d.size);
            const double fc = clampd (swept * (1 + wobble), 32, sr * 0.45);
            d.ph = wrap01 (d.ph + fc * (0.42 + dripRes * 0.5) * d.rate / sr);
            const double g = std::tan (PI * fc / sr), k = 1 / dripQ, a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const double gb = std::tan (PI * clampd (fc * (1.5 + d.ratio), 40, sr * 0.45) / sr), kb = 1 / std::max (1.6, dripQ * 0.62);
            const double b1 = 1 / (1 + gb * (gb + kb)), b2 = gb * b1, b3 = gb * b2;
            double exc = mono * drive * d.env * d.dGuard;
            if (oscLevel > 0.0001)   // EXCITER tone layer, like the synth's oscillator
            {
                const double ph = d.oph, dt = d.freq / sr; d.oph = wrap01 (ph + dt);
                double y;
                if (wave == Wave::sine) y = std::sin (TAU * ph);
                else if (wave == Wave::square) y = ph < 0.5 ? 1 : -1;
                else if (wave == Wave::noise) { const double wn = rng.next() * 2 - 1; y = wn * (1 - hold) + (double) d.noise[(size_t) (int) (ph * 64)] * hold; }
                else { y = 2 * ph - 1; if (ph < dt) { const double t = ph / dt; y -= t + t - t * t - 1; } else if (ph > 1 - dt) { const double t = (ph - 1) / dt; y -= t * t + t + t + 1; } }
                exc += y * oscLevel * 0.35 * d.env * d.dGuard;
            }
            if (d.age < 4 && ping > 0.001) exc += (d.age < 2 ? 0.8 : 0.18) * ping * d.size;
            const double v3 = exc - d.ic2, v1 = a1 * d.ic1 + a2 * v3, v2 = d.ic2 + a2 * d.ic1 + a3 * v3;
            d.ic1 = 2 * v1 - d.ic1; d.ic2 = 2 * v2 - d.ic2;
            if (! std::isfinite (d.ic1) || d.ic1 > 6 || d.ic1 < -6) { d.ic1 = 0; d.ic2 = 0; }
            const double w3 = exc - d.ic2b, w1 = b1 * d.ic1b + b2 * w3, w2 = d.ic2b + b2 * d.ic1b + b3 * w3;
            d.ic1b = 2 * w1 - d.ic1b; d.ic2b = 2 * w2 - d.ic2b;
            if (! std::isfinite (d.ic1b) || d.ic1b > 6 || d.ic1b < -6) { d.ic1b = 0; d.ic2b = 0; }
            double shaped = (v1 * 0.74 + v2 * 0.28 + w1 * bubble) * norm;
            const double pk = std::abs (shaped);
            d.dgLvl = pk > d.dgLvl ? pk : d.dgLvl * 0.9997;
            if (dguard > 0.001)
            {
                if (d.dgLvl > 2.6 - dguard * 1.4) d.dGuard = std::max (0.4 - dguard * 0.2, d.dGuard * 0.997);
                else if (d.dGuard < 1) d.dGuard = std::min (1.0, d.dGuard + 0.00002);
            }
            else d.dGuard = 1;
            if (d.dgLvl > 6) { d.ic1 *= 0.5; d.ic2 *= 0.5; d.ic1b *= 0.5; d.ic2b *= 0.5; d.dgLvl *= 0.5; cutFlash = true; }
            if (d.dGuard != 1) shaped *= d.dGuard;
            if (d.dgLvl > blockPeak) blockPeak = d.dgLvl;
            const double y = std::tanh (shaped * (0.5 + d.size * 0.7) * 1.35) * std::min (1.0, d.env * 4);
            wl += y * d.gl; wr += y * d.gr;
        }
        gate += (gateNow - gate) * (gateNow > gate ? kGateA : kGateR);
        const double dryGain = dryAmt * (1 - chop * (1 - std::min (1.0, gate * 1.6)));
        double ol = xl * dryGain + wl * wetAmt * 0.7, orr = xr * dryGain + wr * wetAmt * 0.7;
        if (fOn)
        {
            if (fstage == 1) { fenv += (1.02 - fenv) * fAk; if (fenv >= 1) { fenv = 1; fstage = 2; } }
            else fenv += (0 - fenv) * fDk;
            if ((i & 7) == 0) fg = std::tan (PI * clampd (p.fCut * std::pow (2.0, keyOct + fEnvOct * fenv + fDepth * std::sin (lfoPh * TAU)), 20, sr * 0.45) / sr);
            if (fDrive > 0.001) { ol = std::tanh (ol * fDg) * fDn; orr = std::tanh (orr * fDg) * fDn; }
            const double gg = fg, kk = fType == FilterType::lp24 ? fk24 : fk;
            auto pick = [&] (double x, int o)
            {
                double band, low; svf (fst, o, x, gg, kk, band, low);
                switch (fType)
                {
                    case FilterType::lp: return low;
                    case FilterType::lp24: { double b2b, l2; svf (fst, o + 4, low, gg, kk, b2b, l2); return l2; }
                    case FilterType::hp: return x - kk * band - low;
                    case FilterType::bp: return band * bpGain;
                    default: return x - kk * band;
                }
            };
            ol = pick (ol, 0); orr = pick (orr, 2);
            if (! std::isfinite (ol) || ! std::isfinite (orr)) { fst.fill (0); ol = orr = 0; }
        }
        L[i] = (float) (ol * p.level); R[i] = (float) (orr * p.level);
    }
    peak = std::max (peak * 0.9, blockPeak);
}
} // namespace fluidic
