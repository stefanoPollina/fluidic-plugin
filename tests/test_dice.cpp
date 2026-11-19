#include "DiceRoller.h"
#include <cstdio>
#include <map>
#include <cmath>
using namespace fluidic;
int main (int argc, char** argv)
{
    std::map<std::string, float> P; std::vector<std::string> avail;
    for (auto& s : paramTable()) if (! s.synthOnly) { P[s.id] = s.defFx; avail.push_back (s.id); }
    int sets = 0;
    DiceRoller::Getter get = [&] (const std::string& id) { return P[id]; };
    DiceRoller::Setter set = [&] (const std::string& id, float v) { P[id] = v; ++sets; };
    DiceRoller d (avail, true, 1234);
    // 1) masks, printed for comparison with the page
    const char* modes[] = { "steps","8ths","beats","half","bars","offbeats","backbeat","tresillo","clave32","clave23","rumba","bossa","dotted","gallop","euclid5","euclid7","euclid9" };
    FILE* f = fopen ("masks_cpp.txt", "w");
    for (auto m : modes) for (int len : { 4, 8, 16, 32 }) { auto v = d.maskFor (m, len); fprintf (f, "%s %d ", m, len); for (bool b : v) fputc (b ? '1' : '0', f); fputc ('\n', f); }
    fclose (f);
    // 2) holds: SPACE panel, ENV row, one dial, HOLD GRID + HOLD PATTERN; cursed d20 every step for 8 bars
    AutoConfig c; c.on = true; c.die = "cursed"; c.mode = "steps";
    c.holdSecs = { "fx" }; c.holdIds = { "filt.env", "filt.attack", "filt.decay", "drip.ping" }; c.holdRate = true; c.holdPattern = true;
    d.setConfig (c);
    auto before = P;
    for (long long k = 0; k < 16 * 8; ++k) d.onGridStep (k, get, set);
    int rolls = (int) d.takeNotices().size();
    std::vector<std::string> heldMoved, freeMoved, bad;
    for (auto& [id, v] : P)
    {
        const bool held = id.rfind ("fx.", 0) == 0 || c.holdIds.count (id) || id == "rhy.rate" || id == "drip.pattern" || id == "out.level" || id == "drip.edge" || id == "rhy.bpm";
        if (v != before[id]) (held ? heldMoved : freeMoved).push_back (id);
        for (auto& s : paramTable()) if (id == s.id && (v < s.min - 1e-6 || v > s.max + 1e-6 || (s.kind != Kind::number && v != std::round (v)))) bad.push_back (id);
    }
    printf ("cursed every step, 8 bars: %d rolls (expect 128) | held that moved: %zu | free that moved: %zu | out of range: %zu\n", rolls, heldMoved.size(), freeMoved.size(), bad.size());
    for (auto& x : heldMoved) printf ("  HELD MOVED %s\n", x.c_str());
    // 3) every style rolls exactly its mask count over 2 bars, at each grid
    int bad2 = 0;
    for (auto m : modes) for (int rate = 0; rate < 4; ++rate)
    {
        DiceRoller e (avail, true, 7); AutoConfig cc; cc.on = true; cc.die = "d4"; cc.mode = m; cc.holdRate = true; e.setConfig (cc);
        P["rhy.rate"] = (float) rate; const int len = 4 * (1 << rate);
        for (long long k = 100; k < 100 + 2 * len; ++k) e.onGridStep (k, get, set);
        int want = 0; for (bool b : e.maskFor (m, len)) want += b; want *= 2;
        const int got = (int) e.takeNotices().size();
        if (got != want) { ++bad2; printf ("  %s rate %d: %d rolls, want %d\n", m, rate, got, want); }
    }
    printf ("style roll counts: %d mismatches\n", bad2);
    // 4) the d20 / d8 / SET mix stays in range over many rolls; d8 on FX touches rhy.rate; off = no rolls
    DiceRoller g (avail, true, 99); AutoConfig cg; g.setConfig (cg);
    for (auto die : { "d4", "d6", "d8", "d12", "d20", "all" }) for (int i = 0; i < 400; ++i) g.roll (die, get, set);
    int oob = 0; for (auto& [id, v] : P) for (auto& s : paramTable()) if (id == s.id && (v < s.min - 1e-4 || v > s.max + 1e-4 || std::isnan (v))) ++oob;
    printf ("2400 rolls of every die: out of range %d | STORM picked (safe dice): %s\n", oob, "n/a");
    sets = 0; for (long long k = 0; k < 64; ++k) g.onGridStep (k, get, set);
    printf ("auto-roll off: %d changes\n", sets);
    // 5) custom mask keeps its shape when the grid changes
    DiceRoller h (avail, true, 5); AutoConfig ch; ch.on = true; ch.die = "d4"; ch.mode = "custom"; ch.customMask = "1000000010100000"; h.setConfig (ch);
    P["rhy.rate"] = 2; h.onGridStep (0, get, set); auto m16 = h.maskString();
    P["rhy.rate"] = 3; h.onGridStep (1, get, set); auto m32 = h.maskString();
    printf ("custom 1/16 %s -> 1/32 %s\n", m16.c_str(), m32.c_str());
}
