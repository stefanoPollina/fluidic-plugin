#include "dsp/FluidicFxEngine.h"
#include <cstdio>
#include <vector>
#include <string>
using namespace fluidic;
int main()
{
    for (double Cl : { 0.0, 1.0 })
    {
        FluidicFxEngine e; e.prepare (48000); e.seed (2);
        auto& p = e.params(); p.pattern = Cl == 0 ? Pattern::even : Pattern::rain; p.rate = Cl == 0 ? 1 : 4; p.density = 0.7; p.bpm = Cl == 0 ? 30 : 60; p.follow = 0; p.ripple = 0.8; p.collide = Cl; p.width = 0.6;
        const int B = 64; std::vector<float> L (B, 0.f), R (B, 0.f); long long t = 0; double last = -1;
        int pulses = 0, coll = 0;
        if (Cl == 0) std::printf ("one drip, RIPPLE 0.8 (time ms · gap since last ms · size · place):\n");
        for (int b = 0; b < 48000 * (Cl == 0 ? 3 : 4) / B; ++b)
        {
            e.process (L.data(), R.data(), L.data(), R.data(), B); t += B;
            for (auto& ev : e.events)
            {
                const double ms = t / 48.0;
                if (ev.kind == DropKind::stream) ++pulses; if (ev.kind == DropKind::splash) ++coll;
                if (Cl == 0) std::printf ("  %7.0f  %6s  %.2f  %.2f%s\n", ms, last < 0 ? "" : std::to_string ((int) (ms - last)).c_str(), ev.size, ev.x, ev.kind == DropKind::rain ? "  ← the drip" : "");
                last = ms;
            }
            e.events.clear();
        }
        if (Cl > 0) std::printf ("4 s, one drip a second, RIPPLE 0.8 + COLLIDE 1: %d ripple pulses, %d collisions\n", pulses, coll);
    }
}
