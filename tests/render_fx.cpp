// Parity test for the FX engine: reads a float32 stereo-interleaved input, renders it, writes the output.
// usage: render_fx <case.txt> <in.raw> <out.raw>
#include "../Source/dsp/FluidicFxEngine.h"
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
using namespace fluidic;
int main (int argc, char** argv)
{
    if (argc < 4) return 1;
    FluidicFxEngine e; e.prepare (48000);
    auto& p = e.params();
    std::map<std::string, Pattern> P { {"even",Pattern::even},{"rain",Pattern::rain},{"both",Pattern::both},{"3d",Pattern::threeD},{"splash",Pattern::splash},{"stream",Pattern::stream},{"storm",Pattern::storm} };
    std::map<std::string, FilterType> F { {"off",FilterType::off},{"lp",FilterType::lp},{"lp24",FilterType::lp24},{"hp",FilterType::hp},{"bp",FilterType::bp},{"notch",FilterType::notch} };
    std::map<std::string, double*> D { {"bpm",&p.bpm},{"density",&p.density},{"sense",&p.sense},{"cutoff",&p.cutoff},{"res",&p.res},{"ping",&p.ping},{"sweep",&p.sweep},{"fall",&p.fall},{"bubble",&p.bubble},{"wobble",&p.wobble},{"chirp",&p.chirp},{"width",&p.width},{"size",&p.size},{"edge",&p.edge},{"key",&p.key},{"mix",&p.mix},{"chop",&p.chop},{"drive",&p.drive},{"fCut",&p.fCut},{"fRes",&p.fRes},{"fDrive",&p.fDrive},{"fEnv",&p.fEnv},{"fA",&p.fA},{"fD",&p.fD},{"fRate",&p.fRate},{"fDepth",&p.fDepth},{"level",&p.level} };
    std::ifstream in (argv[1]); std::string line; unsigned seed = 12345; std::vector<std::pair<double,int>> notes;
    while (std::getline (in, line))
    {
        std::istringstream ss (line); std::string k; ss >> k; if (k.empty()) continue;
        if (k == "seed") ss >> seed;
        else if (k == "rate") ss >> p.rate;
        else if (k == "pattern") { std::string v; ss >> v; p.pattern = P.at (v); }
        else if (k == "fType") { std::string v; ss >> v; p.fType = F.at (v); }
        else if (D.count (k)) ss >> *D[k];
        else { std::fprintf (stderr, "unknown key %s\n", k.c_str()); return 2; }
    }
    e.seed (seed);
    std::FILE* fi = std::fopen (argv[2], "rb"); std::vector<float> inter;
    float buf[4096]; size_t got; while ((got = std::fread (buf, 4, 4096, fi)) > 0) inter.insert (inter.end(), buf, buf + got); std::fclose (fi);
    const int total = (int) inter.size() / 2;
    std::vector<float> L ((size_t) total), R ((size_t) total);
    for (int i = 0; i < total; ++i) { L[(size_t) i] = inter[(size_t) i * 2]; R[(size_t) i] = inter[(size_t) i * 2 + 1]; }
    for (int i = 0; i < total; i += 128) { const int b = std::min (128, total - i); e.process (L.data() + i, R.data() + i, L.data() + i, R.data() + i, b); }
    std::vector<float> out; out.reserve ((size_t) total * 2);
    for (int i = 0; i < total; ++i) { out.push_back (L[(size_t) i]); out.push_back (R[(size_t) i]); }
    std::FILE* fo = std::fopen (argv[3], "wb"); std::fwrite (out.data(), 4, out.size(), fo); std::fclose (fo);
    return 0;
}
