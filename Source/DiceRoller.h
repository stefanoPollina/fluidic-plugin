#pragma once
/* FLUIDIC FX — the dice and AUTO ROLL, inside the plugin, so auto-roll keeps going with the window closed.
   A port of the web prototype's roll() / rollAll() / auto-roll grid (index.html): same dice, same safe ranges,
   same roll styles, same HOLDs. Plain C++ (no JUCE) so it can be tested on its own; the processor reads and
   writes the host parameters through the get/set callbacks. Runs on the message thread. */
#include <cstdint>
#include <functional>
#include <random>
#include <set>
#include <string>
#include <vector>
#include "ParamTable.h"

namespace fluidic
{
struct AutoConfig
{
    bool on = false;
    std::string die = "all";        // d4 d6 d8 d12 d20 cursed all
    std::string mode = "beats";     // a roll style key (see maskFor), or "custom"
    std::string customMask;         // "1010…" when mode is custom
    bool holdRate = false, holdPattern = false;
    std::set<std::string> holdSecs, holdIds;
};

struct RollNotice { std::string label; int n = 0; };

class DiceRoller
{
public:
    using Getter = std::function<float (const std::string&)>;          // real value (choices: index)
    using Setter = std::function<void (const std::string&, float)>;    // real value (choices: index)

    // available: the parameters this plugin has (its own dials)
    DiceRoller (const std::vector<std::string>& available, bool isFx, uint32_t seed);

    void setConfig (const AutoConfig& c);
    const AutoConfig& config() const { return cfg; }

    // the drip grid's step counter; rolls on every step the style says, since the last call
    void onGridStep (long long k, const Getter& get, const Setter& set);

    // one roll, as if the die were pressed (quiet rolls show a small badge in the window)
    RollNotice roll (const std::string& key, const Getter& get, const Setter& set);

    std::vector<RollNotice> takeNotices() { auto n = std::move (notices); notices.clear(); return n; }
    int position() const { return pos; }
    std::string maskString() const { std::string s; for (bool b : mask) s += b ? '1' : '0'; return s; }

    // for tests: the rhythm a style gives on a bar of len steps
    std::vector<bool> maskFor (const std::string& mode, int len);

private:
    bool isFree (const std::string& id) const;
    void rollParam (const std::string& id, bool cursed, std::vector<float>& v);
    void nudge (const std::string& id, double amt, std::vector<float>& v);
    void resizeMask (int len);
    double rnd() { return uni (rng); }
    int index (const std::string& id) const;

    const std::vector<ParamSpec>& T;
    bool fx;
    std::vector<std::string> pool;   // what the dice may touch
    AutoConfig cfg;
    std::vector<bool> mask;
    long long lastStep = 0; bool haveLast = false;
    int pos = -1;
    std::vector<RollNotice> notices;
    std::mt19937 rng;
    std::uniform_real_distribution<double> uni { 0.0, 1.0 };
};
} // namespace fluidic
