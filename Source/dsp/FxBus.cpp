#include "FxBus.h"
#include <cmath>

namespace fluidic
{
namespace
{
// Gain (dB) of Chrome's DynamicsCompressor with the prototype's settings (threshold -2, ratio 20, knee 2,
// attack 2 ms, release 120 ms), measured with steady input from -30 dB to +14 dB in 0.5 dB steps
// (tools/chrome_limiter.json). Includes Chrome's automatic makeup gain, so levels match the prototype.
constexpr double curveLo = -30, curveStep = 0.5;
constexpr double chromeGainDb[] = { 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.8043, 0.6638, 0.3404, -0.0752, -0.5364, -1.0114, -1.4864, -1.9614, -2.4364, -2.9114, -3.3864, -3.8614, -4.3364, -4.8114, -5.2863, -5.7613, -6.2363, -6.7113, -7.1863, -7.6613, -8.1363, -8.6113, -9.0863, -9.5613, -10.0363, -10.5113, -10.9863, -11.4613, -11.9363, -12.4113, -12.8863, -13.3613, -13.8363 };
constexpr int curveN = (int) (sizeof (chromeGainDb) / sizeof (double));
double gainDb (double x)
{
    const double t = (x - curveLo) / curveStep;
    if (t <= 0) return chromeGainDb[0];
    if (t >= curveN - 1) return chromeGainDb[curveN - 1] - (t - (curveN - 1)) * curveStep * 0.95;   // ratio 20 beyond the table
    const int i = (int) t; const double f = t - i;
    return chromeGainDb[i] + (chromeGainDb[i + 1] - chromeGainDb[i]) * f;
}
}

void FxBus::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    kGain = 1 - std::exp (-1 / (0.02 * sr));
    kTime = 1 - std::exp (-1 / (0.03 * sr));
    len = (int) std::ceil (sr * 2.1) + 256;
    lineA.assign ((size_t) len, 0); lineB.assign ((size_t) len, 0);
    // Web Audio lowpass: alpha = sin(w0) / (2 * 10^(Q/20)), Q = 1 (dB)
    const double w0 = 2 * juce::MathConstants<double>::pi * 5200 / sr, cw = std::cos (w0), alpha = std::sin (w0) / (2 * std::pow (10.0, 1.0 / 20));
    const double a0 = 1 + alpha;
    tb0 = (1 - cw) / 2 / a0; tb1 = (1 - cw) / a0; tb2 = (1 - cw) / 2 / a0; ta1 = -2 * cw / a0; ta2 = (1 - alpha) / a0;
    reverb.prepare ({ sr, (juce::uint32) maxBlock, 2 });
    wet.setSize (2, maxBlock);
    kAtt = 1 - std::exp (-1 / (limAtt * sr));
    kRel = 1 - std::exp (-1 / (limRel * sr));
    look = std::max (1, (int) std::lround (0.006 * sr));   // Chrome's compressor looks 6 ms ahead (288 samples at 48 kHz)
    lookL.assign ((size_t) look, 0); lookR.assign ((size_t) look, 0); lookPos = 0;
    qCap = look + 2; qVal.assign ((size_t) qCap, 0); qIdx.assign ((size_t) qCap, 0); qHead = qCount = 0; sampleCount = 0;
    cur = target;
    reset();
}

void FxBus::reset()
{
    std::fill (lineA.begin(), lineA.end(), 0.0f); std::fill (lineB.begin(), lineB.end(), 0.0f);
    pos = 0; tz1 = tz2 = 0; grDb = gainDb (-200); reverb.reset();
    std::fill (lookL.begin(), lookL.end(), 0.0f); std::fill (lookR.begin(), lookR.end(), 0.0f); lookPos = 0;
    qHead = qCount = 0; sampleCount = 0;
}

float FxBus::webAudioNormalisation (const juce::AudioBuffer<float>& ir, double sampleRate)
{
    double power = 0;
    for (int c = 0; c < ir.getNumChannels(); ++c)
        for (int i = 0; i < ir.getNumSamples(); ++i) { const double s = ir.getSample (c, i); power += s * s; }
    power = std::sqrt (power / (ir.getNumChannels() * (double) ir.getNumSamples()));
    if (! std::isfinite (power) || power < 0.000125) power = 0.000125;
    double scale = 1 / power * 0.00125 * (44100.0 / sampleRate);
    if (ir.getNumChannels() == 4) scale *= 0.5;
    return (float) scale;
}

void FxBus::loadReverb (double seconds, uint32_t seed)
{
    Rng rng; rng.seed (seed);
    const int n = std::max (1, (int) std::floor (sr * seconds));
    juce::AudioBuffer<float> ir (2, n);
    for (int c = 0; c < 2; ++c)
    {
        auto* d = ir.getWritePointer (c);
        for (int i = 0; i < n; ++i) d[i] = (float) ((rng.next() * 2 - 1) * std::pow (1 - (double) i / n, 2.6));
    }
    ir.applyGain (webAudioNormalisation (ir, sr));
    reverb.loadImpulseResponse (std::move (ir), sr, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    revLoaded = seconds;
}

float FxBus::readLine (const std::vector<float>& line, double d) const
{
    double rp = pos - d; while (rp < 0) rp += len;
    const int i0 = (int) rp; const int i1 = (i0 + 1) % len; const double fr = rp - i0;
    return (float) (line[(size_t) i0] + (line[(size_t) i1] - line[(size_t) i0]) * fr);
}

void FxBus::process (float* L, float* R, int n)
{
    // reverb send (block-rate gain ramp is inaudible next to the prototype's 20 ms smoothing)
    const double revStart = cur.revMix;
    double revEnd = revStart;
    for (int i = 0; i < n; ++i) revEnd += (target.revMix - revEnd) * kGain;
    wet.setSize (2, n, false, false, true);
    for (int i = 0; i < n; ++i)
    {
        const float g = (float) (revStart + (revEnd - revStart) * (i + 1) / n);
        wet.setSample (0, i, L[i] * g); wet.setSample (1, i, R[i] * g);
    }
    cur.revMix = revEnd;
    {
        juce::dsp::AudioBlock<float> blk (wet.getArrayOfWritePointers(), 2, (size_t) n);
        reverb.process (juce::dsp::ProcessContextReplacing<float> (blk));
    }

    for (int i = 0; i < n; ++i)
    {
        cur.delayMix += (target.delayMix - cur.delayMix) * kGain;
        cur.delayFb += (target.delayFb - cur.delayFb) * kGain;
        cur.delayTime += (target.delayTime - cur.delayTime) * kTime;
        const double dIn = L[i], dInR = R[i];
        // delay send -> tone (on the mid signal, as the prototype's channel merger does)
        const double x = 0.5 * (dIn + dInR) * cur.delayMix;
        const double y = tb0 * x + tz1; tz1 = tb1 * x - ta1 * y + tz2; tz2 = tb2 * x - ta2 * y;
        const double ds = std::max (128.0, std::min (cur.delayTime * sr, len - 3.0));
        // the browser runs its delay feedback cycle one 128-sample block late on the left→right hop;
        // matching it keeps the echoes exactly where the prototype puts them
        const float a = readLine (lineA, ds), b = readLine (lineB, ds + 128);
        lineA[(size_t) pos] = (float) (y + cur.delayFb * b);
        lineB[(size_t) pos] = a;
        pos = (pos + 1) % len;

        double outL = dIn + a + wet.getSample (0, i);
        double outR = dInR + b + wet.getSample (1, i);
        // limiter: the detector hears the signal 6 ms before it is played, as Chrome's does,
        // and follows the loudest moment in that window so the gain is down before a peak arrives
        const float now = (float) std::max (std::abs (outL), std::abs (outR));
        while (qCount > 0 && qVal[(size_t) ((qHead + qCount - 1) % qCap)] <= now) --qCount;
        qVal[(size_t) ((qHead + qCount) % qCap)] = now; qIdx[(size_t) ((qHead + qCount) % qCap)] = sampleCount; ++qCount;
        while (qIdx[(size_t) qHead] <= sampleCount - look) { qHead = (qHead + 1) % qCap; --qCount; }
        ++sampleCount;
        const double lvl = qVal[(size_t) qHead];
        const double want = gainDb (lvl > 1e-9 ? 20 * std::log10 (lvl) : -180);
        grDb += (want - grDb) * (want < grDb ? kAtt : kRel);
        const double g = std::pow (10.0, grDb / 20);
        const float dl = lookL[(size_t) lookPos], dr = lookR[(size_t) lookPos];
        lookL[(size_t) lookPos] = (float) outL; lookR[(size_t) lookPos] = (float) outR;
        lookPos = (lookPos + 1) % look;
        L[i] = (float) (dl * g); R[i] = (float) (dr * g);
    }
}
} // namespace fluidic
