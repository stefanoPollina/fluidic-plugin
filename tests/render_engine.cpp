// Parity test: renders the engine exactly like the browser test does, writes float32 stereo interleaved.
// usage: render_engine <params.txt> <out.raw>   (params.txt: "key value" lines, plus "notes 48 55 ...", "seconds 4", "seed 12345")
#include "../Source/dsp/FluidicEngine.h"
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
using namespace fluidic;
int main (int argc, char** argv)
{
    if (argc < 3) return 1;
    FluidicEngine e; e.prepare (48000);
    auto& p = e.params();
    std::ifstream in (argv[1]); std::string line; std::vector<int> notes; double secs = 4; unsigned seed = 12345; double vel = 0.8;
    std::map<std::string, Wave> W { {"sine",Wave::sine},{"saw",Wave::saw},{"square",Wave::square},{"noise",Wave::noise} };
    std::map<std::string, Pattern> P { {"even",Pattern::even},{"rain",Pattern::rain},{"both",Pattern::both},{"3d",Pattern::threeD},{"splash",Pattern::splash},{"stream",Pattern::stream},{"storm",Pattern::storm} };
    std::map<std::string, FilterType> F { {"off",FilterType::off},{"lp",FilterType::lp},{"lp24",FilterType::lp24},{"hp",FilterType::hp},{"bp",FilterType::bp},{"notch",FilterType::notch} };
    std::map<std::string, double*> D { {"oscLevel",&p.oscLevel},{"hold",&p.hold},{"octave",&p.octave},{"cutoff",&p.cutoff},{"res",&p.res},{"key",&p.key},{"ping",&p.ping},{"sweep",&p.sweep},{"fall",&p.fall},{"bubble",&p.bubble},{"wobble",&p.wobble},{"chirp",&p.chirp},{"width",&p.width},{"density",&p.density},{"size",&p.size},{"edge",&p.edge},{"fCut",&p.fCut},{"fRes",&p.fRes},{"fDrive",&p.fDrive},{"fKey",&p.fKey},{"fEnv",&p.fEnv},{"fA",&p.fA},{"fD",&p.fD},{"fRate",&p.fRate},{"fDepth",&p.fDepth},{"a",&p.a},{"d",&p.d},{"s",&p.s},{"r",&p.r},{"level",&p.level} };
    while (std::getline (in, line))
    {
        std::istringstream ss (line); std::string k; ss >> k; if (k.empty()) continue;
        if (k == "notes") { int n; while (ss >> n) notes.push_back (n); }
        else if (k == "seconds") ss >> secs;
        else if (k == "seed") ss >> seed;
        else if (k == "vel") ss >> vel;
        else if (k == "wave") { std::string v; ss >> v; p.wave = W.at (v); }
        else if (k == "pattern") { std::string v; ss >> v; p.pattern = P.at (v); }
        else if (k == "fType") { std::string v; ss >> v; p.fType = F.at (v); }
        else if (D.count (k)) ss >> *D[k];
        else { std::fprintf (stderr, "unknown key %s\n", k.c_str()); return 2; }
    }
    e.seed (seed);
    for (int n : notes) e.noteOn (n, vel);
    const int total = (int) (48000 * secs);
    std::vector<float> L ((size_t) total), R ((size_t) total);
    for (int i = 0; i < total; i += 128) e.process (L.data() + i, R.data() + i, std::min (128, total - i));
    std::vector<float> out; out.reserve ((size_t) total * 2);
    for (int i = 0; i < total; ++i) { out.push_back (L[(size_t) i]); out.push_back (R[(size_t) i]); }
    std::FILE* f = std::fopen (argv[2], "wb"); std::fwrite (out.data(), 4, out.size(), f); std::fclose (f);
    return 0;
}
