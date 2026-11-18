#pragma once
/* FLUIDIC FX — the drip engine as an audio effect. Port of the web prototype's fluidic-fx-worklet.js:
   incoming audio is what the drip resonators ring with; a tempo grid drops drips in the chosen pattern;
   CHOP gates the dry signal to the drip rhythm; SENSE lets the input's own hits trigger drips. */
#include "Rng.h"
#include "FluidicEngine.h"   // shared enums and DropEvent
#include <array>
#include <vector>

namespace fluidic
{
struct FxEngineParams
{
    Pattern pattern = Pattern::even; int rate = 4; double bpm = 120, density = 0.5, sense = 0;
    double cutoff = 900, res = 0.8, ping = 0.6, sweep = 2.2, fall = 0.08, bubble = 0.55, wobble = 0.3;
    double chirp = 0, width = 0.4, size = 0.5, edge = 0, key = 1;
    double mix = 0.5, chop = 0, drive = 1;
    FilterType fType = FilterType::off;
    double fCut = 2400, fRes = 0.25, fDrive = 0, fEnv = 0, fA = 0.005, fD = 0.25, fRate = 0.15, fDepth = 0;
    double level = 0.8;
    Wave wave = Wave::saw; double oscLevel = 0, hold = 0, octave = 0;   // EXCITER tone layer (LEVEL 0 = the input only)
    double a = 0.001, d = 0.4, s = 1, r = 4, fKey = 0;                 // AMP per drip (neutral by default), filter KEY
};

class FluidicFxEngine
{
public:
    static constexpr int maxDrops = 12;
    void prepare (double sampleRate);
    void seed (uint32_t s) { rng.seed (s); }
    FxEngineParams& params() { return p; }

    void noteOn (int note);      // keys / MIDI: a drip at that pitch, and later drips follow the held key
    void noteOff (int note);
    void panic();
    void sync() { clock = 0; step = 0; pending.clear(); }
    // host transport: put the grid at this position (in grid steps since the song start)
    void setGridPosition (double stepsFromStart);
    void hostStopped() { gridFresh = true; }   // next play start re-locks, and fires a step that starts right there

    // in and out may be the same buffers
    void process (const float* inL, const float* inR, float* outL, float* outR, int n);

    std::vector<DropEvent> events;
    double peak = 0; bool cutFlash = false;
    int activeDrops() const;
    long long gridStep() const { return step; }   // for the window's auto-roll

private:
    struct Drop
    {
        bool active = false; double age = 0, size = 0.5, jit = 0, ratio = 0, amp = 0, rate = 1, fc = 900, ph = 0, gl = 0.707, gr = 0.707;
        double ic1 = 0, ic2 = 0, ic1b = 0, ic2b = 0, env = 0, len = 1, dGuard = 1, dgLvl = 0;
        int note = 60; double freq = 261.6, oph = 0; std::array<float, 64> noise {}; double aenv = 0; int astage = 1; double gate = 1;
    };
    struct Pending { double at; double size; DropKind kind; };

    void spawn (double size, DropKind kind, int note = -1);
    void onStep (long long k, double spb);
    void renderBlock (const float* inL, const float* inR, float* L, float* R, int n);

    FxEngineParams p;
    std::array<Drop, maxDrops> drops;
    std::vector<Pending> pending;
    std::vector<int> held;
    Rng rng;
    double sr = 48000, clock = 0, fast = 0, slow = 0, refractory = 0, gate = 0, lfoPh = 0; int lastNote = 60;
    long long step = 0; bool gridFresh = true;
    std::array<double, 8> fst {}; double fenv = 0, fg = 0.1; int fstage = 0;
};
} // namespace fluidic
