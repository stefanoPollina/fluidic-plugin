// Offline render of engine + effects bus, for comparing the full chain with the web prototype.
// usage: fluidic_render <case.txt> <out.raw>   (case.txt as for render_engine, plus delayMix/delayTime/delayFb/revMix/revSize, "revseed N")
#include <juce_dsp/juce_dsp.h>
#include "dsp/FluidicEngine.h"
#include "dsp/FxBus.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
using namespace fluidic;
int main (int argc, char** argv)
{
    if (argc < 3) return 1;
    FluidicEngine e; e.prepare (48000);
    FxBus fx; FxParams f;
    auto& p = e.params();
    std::ifstream in (argv[1]); std::string line; std::vector<int> notes; double secs = 4; unsigned seed = 12345, revSeed = 99;
    std::map<std::string, Wave> W { {"sine",Wave::sine},{"saw",Wave::saw},{"square",Wave::square},{"noise",Wave::noise} };
    std::map<std::string, Pattern> P { {"even",Pattern::even},{"rain",Pattern::rain},{"both",Pattern::both},{"3d",Pattern::threeD},{"splash",Pattern::splash},{"stream",Pattern::stream},{"storm",Pattern::storm} };
    std::map<std::string, FilterType> F { {"off",FilterType::off},{"lp",FilterType::lp},{"lp24",FilterType::lp24},{"hp",FilterType::hp},{"bp",FilterType::bp},{"notch",FilterType::notch} };
    std::map<std::string, double*> D { {"oscLevel",&p.oscLevel},{"hold",&p.hold},{"octave",&p.octave},{"cutoff",&p.cutoff},{"res",&p.res},{"key",&p.key},{"ping",&p.ping},{"sweep",&p.sweep},{"fall",&p.fall},{"bubble",&p.bubble},{"wobble",&p.wobble},{"chirp",&p.chirp},{"width",&p.width},{"density",&p.density},{"size",&p.size},{"edge",&p.edge},{"fCut",&p.fCut},{"fRes",&p.fRes},{"fDrive",&p.fDrive},{"fKey",&p.fKey},{"fEnv",&p.fEnv},{"fA",&p.fA},{"fD",&p.fD},{"fRate",&p.fRate},{"fDepth",&p.fDepth},{"a",&p.a},{"d",&p.d},{"s",&p.s},{"r",&p.r},{"level",&p.level},
        {"delayMix",&f.delayMix},{"delayTime",&f.delayTime},{"delayFb",&f.delayFb},{"revMix",&f.revMix},{"revSize",&f.revSize} };
    while (std::getline (in, line))
    {
        std::istringstream ss (line); std::string k; ss >> k; if (k.empty()) continue;
        if (k == "notes") { int n; while (ss >> n) notes.push_back (n); }
        else if (k == "seconds") ss >> secs;
        else if (k == "seed") ss >> seed;
        else if (k == "revseed") ss >> revSeed;
        else if (k == "wave") { std::string v; ss >> v; p.wave = W.at (v); }
        else if (k == "pattern") { std::string v; ss >> v; p.pattern = P.at (v); }
        else if (k == "fType") { std::string v; ss >> v; p.fType = F.at (v); }
        else if (D.count (k)) ss >> *D[k];
        else { std::fprintf (stderr, "unknown key %s\n", k.c_str()); return 2; }
    }
    fx.setParams (f);
    if (const char* a = std::getenv ("LIM_ATT")) fx.setLimiterTimes (std::atof (a), std::atof (std::getenv ("LIM_REL")));
    fx.prepare (48000, 128);
    fx.loadReverb (f.revSize, revSeed);
    juce::Thread::sleep (300);   // the convolution swaps its new impulse in on a background thread
    e.seed (seed);
    for (int n : notes) e.noteOn (n, 0.8);
    const int total = (int) (48000 * secs);
    std::vector<float> L ((size_t) total), R ((size_t) total);
    for (int i = 0; i < total; i += 128)
    {
        const int b = std::min (128, total - i);
        e.process (L.data() + i, R.data() + i, b);
        fx.process (L.data() + i, R.data() + i, b);
    }
    std::vector<float> out; out.reserve ((size_t) total * 2);
    for (int i = 0; i < total; ++i) { out.push_back (L[(size_t) i]); out.push_back (R[(size_t) i]); }
    std::FILE* fo = std::fopen (argv[2], "wb"); std::fwrite (out.data(), 4, out.size(), fo); std::fclose (fo);
    return 0;
}
