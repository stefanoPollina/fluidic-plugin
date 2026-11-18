#pragma once
/* FLUIDIC — drip synthesis engine.
   A line-by-line port of the web prototype's AudioWorklet (fluidic-worklet.js):
   exciter oscillator -> DRIP resonator pair -> FILTER -> amp envelope, 12 voices.
   Pure C++17, no JUCE: the same file runs in the plugin and in the parity test. */
#include "Rng.h"
#include <array>
#include <cstdint>
#include <vector>

namespace fluidic
{
enum class Wave : int { sine, saw, square, noise };
enum class Pattern : int { even, rain, both, threeD, splash, stream, storm };
enum class FilterType : int { off, lp, lp24, hp, bp, notch };

struct EngineParams
{
    Wave wave = Wave::saw; double oscLevel = 0.6, hold = 0, octave = 0;
    Pattern pattern = Pattern::both;
    double cutoff = 900, res = 0.8, key = 0.6, ping = 0.85, sweep = 2.2, fall = 0.055, bubble = 0.55, wobble = 0.4;
    double chirp = 0, width = 0, density = 0.5, size = 0.5, edge = 0;
    FilterType fType = FilterType::off;
    double fCut = 2400, fRes = 0.25, fDrive = 0, fKey = 0.5, fEnv = 0, fA = 0.01, fD = 0.6, fRate = 0.15, fDepth = 0;
    double a = 0.004, d = 0.4, s = 0.7, r = 0.5, level = 0.8;
};

enum class DropKind : uint8_t { note, rain, splash, stream };

struct DropEvent   // one drop, reported to the visualiser
{
    float x, size; DropKind kind; int note; float sweep, chirp;
};

class FluidicEngine
{
public:
    static constexpr int maxVoices = 12;

    void prepare (double sampleRate);
    void seed (uint32_t s) { rng.seed (s); }
    EngineParams& params() { return p; }

    void noteOn (int note, double vel, bool noSteal = false);
    void noteOff (int note);
    void panic();

    // renders n samples (any n); call noteOn/noteOff between calls for sample-accurate timing
    void process (float* outL, float* outR, int n);

    // visualiser feed — drained by the UI side
    std::vector<DropEvent> events;
    double peak = 0; bool cutFlash = false;
    int activeVoices() const;

private:
    enum class Stage : uint8_t { idle, attack, decay, release, steal };
    struct Voice
    {
        bool active = false, gate = false; int id = 0, note = 60; double vel = 1;
        double age = 0, phase = 0, freq = 261.6, env = 0; Stage stage = Stage::idle; double fc = 900, pan = 0;
        double ic1 = 0, ic2 = 0, ic1b = 0, ic2b = 0;
        double dripT0 = 0, dripWait = 0, dripAge = 1e9; int dripHit = 0; double dripSize = 0.5, dripJit = 0, dripRatio = 0, dripAmp = 0, dripRate = 1;
        double dripPh = 0; bool dripKick = false; int splashN = 0; double stW = 0, spW = 0, dripKind = 1, dGuard = 1, dgLvl = 0;
        std::array<float, 32> dripDelay {}; int dripDPos = 0;
        std::array<float, 64> noise {};
        std::array<double, 8> fst {}; double fenv = 0; int fstage = 0; double fg = 0.1;
        double gl = 0.7071, gr = 0.7071; int pendNote = -1; double pendVel = 0; bool pendOff = false;
    };

    void startVoice (Voice& v, int note, double vel);
    double env (Voice& v, double ka, double kd, double kr);
    double osc (Voice& v);
    void drop (const Voice& v, double size, DropKind kind);
    void renderBlock (float* L, float* R, int n);   // n <= 128, same per-block maths as the worklet

    EngineParams p;
    std::array<Voice, maxVoices> voices;
    Rng rng;
    double sr = 48000, lfoPh = 0, stealK = 0;
};
} // namespace fluidic
