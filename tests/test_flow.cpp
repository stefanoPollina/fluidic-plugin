#include "dsp/FluidicEngine.h"
#include "dsp/FluidicFxEngine.h"
#include <cstdio>
#include <cmath>
#include <vector>
using namespace fluidic;
static void synth (const char* name, double S, double T, double C)
{
    FluidicEngine e; e.prepare (48000); e.seed (5);
    auto& p = e.params(); p.pattern = Pattern::rain; p.slosh = S; p.turb = T; p.cascade = C; p.sway = 1.5;
    e.noteOn (60, 0.9); e.noteOn (67, 0.9);
    std::vector<float> L (48000 * 4), R (48000 * 4); e.process (L.data(), R.data(), (int) L.size());
    double bal = 0, l2 = 0, r2 = 0; int n = (int) e.events.size();
    for (size_t i = 0; i < L.size(); ++i) { l2 += L[i] * L[i]; r2 += R[i] * R[i]; }
    // how much the stereo balance moves around, in 0.25 s windows
    std::vector<double> b; for (size_t w = 0; w + 12000 <= L.size(); w += 12000) { double a = 0, c = 0; for (size_t i = w; i < w + 12000; ++i) { a += L[i] * L[i]; c += R[i] * R[i]; } b.push_back ((a - c) / (a + c + 1e-12)); }
    double mn = 1, mx = -1; for (double x : b) { mn = std::min (mn, x); mx = std::max (mx, x); }
    std::printf ("  synth %-24s drips %3d | stereo swing %.2f\n", name, n, mx - mn);
}
static void fx (const char* name, double S, double T, double C)
{
    FluidicFxEngine e; e.prepare (48000); e.seed (5);
    auto& p = e.params(); p.pattern = Pattern::even; p.rate = 4; p.slosh = S; p.turb = T; p.cascade = C; p.follow = 0; p.width = 0.5;
    const int B = 128; std::vector<float> L (B), R (B); int n = 0; std::vector<double> onsets;
    long long t = 0; double lastGrid = -1;
    for (int b = 0; b < 48000 * 4 / B; ++b)
    {
        for (int i = 0; i < B; ++i) L[i] = R[i] = 0.1f * std::sin (t++ * 0.03f);
        e.process (L.data(), R.data(), L.data(), R.data(), B);
        n += (int) e.events.size(); e.events.clear();
    }
    std::printf ("  fx    %-24s drips %3d in 4 s (the grid alone gives 32)\n", name, n);
}
int main()
{
    synth ("FLOW off", 0, 0, 0); synth ("SLOSH 1", 1, 0, 0); synth ("TURB 1", 0, 1, 0); synth ("CASCADE 1", 0, 0, 1);
    fx ("FLOW off", 0, 0, 0); fx ("CASCADE 0.5", 0, 0, 0.5); fx ("CASCADE 1", 0, 0, 1); fx ("TURB 1", 0, 1, 0);
}
