#pragma once
/* FLUIDIC — the overall effects bus, matching the web prototype's Web Audio graph:
     dry ─────────────────────────────────┐
     send → 5.2 kHz lowpass → ping-pong delay ─┤→ limiter → out
     send → reverb (generated impulse) ───┘
   Dial moves are smoothed with the same time constants as the prototype's setTargetAtTime. */
#include <juce_dsp/juce_dsp.h>
#include "Rng.h"

namespace fluidic
{
struct FxParams { double delayMix = 0.18, delayTime = 0.28, delayFb = 0.38, revMix = 0.24, revSize = 2.2; };

class FxBus
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void setParams (const FxParams& f) { target = f; }
    void process (float* L, float* R, int n);

    // message thread: build a new reverb impulse (as the prototype does when SIZE settles)
    void loadReverb (double seconds, uint32_t seed);
    double loadedReverbSize() const { return revLoaded; }

    // the browser's ConvolverNode normalisation (Web Audio spec), so the reverb sits at the same level
    static float webAudioNormalisation (const juce::AudioBuffer<float>& ir, double sampleRate);

private:
    double sr = 48000;
    FxParams target, cur;
    double kGain = 0, kTime = 0;
    // delay: the prototype merges each delay line to mono, so the ping-pong runs on the mid signal
    std::vector<float> lineA, lineB; int pos = 0, len = 0;
    double tz1 = 0, tz2 = 0, tb0 = 0, tb1 = 0, tb2 = 0, ta1 = 0, ta2 = 0;   // 5.2 kHz tone filter
    juce::dsp::Convolution reverb { juce::dsp::Convolution::Latency { 0 } };
    juce::AudioBuffer<float> wet;
    double revLoaded = 0;
    // limiter: Chrome's DynamicsCompressor curve (measured), 6 ms look-ahead; timing fitted to match Chrome
    double grDb = 0, kAtt = 0, kRel = 0, limAtt = 0.004, limRel = 0.010;   // fitted to Chrome (tests/compare_full.py)
    std::vector<float> lookL, lookR; int look = 1, lookPos = 0;
    std::vector<float> qVal; std::vector<long long> qIdx; int qCap = 1, qHead = 0, qCount = 0; long long sampleCount = 0;   // running max over the look-ahead
public:
    int latencySamples() const { return look; }   // reported to the DAW, which compensates for it
    void setLimiterTimes (double attackS, double releaseS) { limAtt = attackS; limRel = releaseS; }
private:
    float readLine (const std::vector<float>& line, double delaySamples) const;
};
} // namespace fluidic
