#include "DiceRoller.h"
#include <cstdio>
#include <map>
using namespace fluidic;
int main()
{
    std::map<std::string, float> P; std::vector<std::string> avail;
    for (auto& s : paramTable()) if (! s.synthOnly) { P[s.id] = s.defFx; avail.push_back (s.id); }
    DiceRoller::Getter get = [&] (const std::string& id) { return P[id]; };
    DiceRoller::Setter set = [&] (const std::string& id, float v) { P[id] = v; };
    DiceRoller d (avail, true, 42);
    AutoConfig c; c.on = true; c.die = "d4"; c.mode = "seq"; c.holdRate = true;
    c.seqDice.assign (32, ""); c.seqDice[0] = "auto"; c.seqDice[4] = "cursed"; c.seqDice[10] = "all"; c.seqDice[20] = "d20";   // step 21 is beyond 16 steps
    d.setConfig (c);
    P["rhy.rate"] = 2;   // 1/16: 16 steps
    std::map<int, std::string> seen; int n = 0;
    for (long long k = 0; k < 32; ++k)
    {
        d.onGridStep (k, get, set);
        for (auto& r : d.takeNotices()) { seen[(int) (k % 16)] += r.label + " "; ++n; }
    }
    printf ("2 bars at 1/16, %d rolls; mask %s\n", n, d.maskString().c_str());
    for (auto& [st, l] : seen) printf ("  step %2d: %s\n", st + 1, l.c_str());
    // the lane changes: config update follows
    c.seqDice[4] = ""; d.setConfig (c); n = 0;
    for (long long k = 32; k < 48; ++k) { d.onGridStep (k, get, set); n += (int) d.takeNotices().size(); }
    printf ("after removing step 5's die: %d rolls in a bar (expect 2)\n", n);
}
