#include "DiceRoller.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace fluidic
{
namespace
{
// the prototype's SAFE ranges: where the dice land unless cursed
const std::map<std::string, std::pair<double, double>>& safeRanges()
{
    static const std::map<std::string, std::pair<double, double>> m {
        { "exc.level", { 0.35, 0.85 } }, { "exc.hold", { 0, 0.5 } }, { "exc.octave", { -1, 1 } },
        { "drip.cutoff", { 180, 3200 } }, { "drip.res", { 0.6, 0.92 } }, { "drip.key", { 0.3, 0.9 } }, { "drip.ping", { 0.4, 1 } }, { "drip.sweep", { -2, 3.5 } },
        { "drip.fall", { 0.03, 0.2 } }, { "drip.bubble", { 0.1, 0.9 } }, { "drip.wobble", { 0, 0.8 } }, { "drip.chirp", { 0, 0.8 } }, { "drip.width", { 0, 0.9 } },
        { "drip.density", { 0.2, 0.75 } }, { "drip.size", { 0.2, 0.85 } },
        { "filt.cutoff", { 350, 9000 } }, { "filt.res", { 0, 0.75 } }, { "filt.drive", { 0, 0.5 } }, { "filt.env", { -0.6, 0.8 } }, { "filt.attack", { 0.002, 1.5 } },
        { "filt.decay", { 0.08, 5 } }, { "filt.key", { 0.2, 0.8 } }, { "filt.lfoRate", { 0.03, 4 } }, { "filt.lfoDepth", { 0, 0.5 } },
        { "amp.a", { 0.001, 0.8 } }, { "amp.d", { 0.08, 2 } }, { "amp.s", { 0.2, 1 } }, { "amp.r", { 0.15, 3 } },
        { "fx.delayMix", { 0, 0.4 } }, { "fx.delayTime", { 0.12, 0.6 } }, { "fx.delayFb", { 0.1, 0.6 } }, { "fx.revMix", { 0.08, 0.6 } }, { "fx.revSize", { 1, 7 } },
        { "in.drive", { 0.7, 1.8 } }, { "in.mix", { 0.35, 0.8 } }, { "in.chop", { 0, 0.8 } }, { "rhy.sense", { 0, 0.6 } } };
    return m;
}
// SAFE_ENUM, as choice indices (repeats weight the pick)
const std::map<std::string, std::vector<int>>& safeChoices()
{
    static const std::map<std::string, std::vector<int>> m {
        { "rhy.rate", { 1, 2, 2, 3 } },                       // 1/8 1/16 1/16 1/32
        { "drip.pattern", { 0, 1, 2, 3, 4, 5 } },             // no STORM
        { "filt.type", { 0, 1, 1, 2, 2, 3, 4, 5 } } };        // off lp lp lp24 lp24 hp bp notch
    return m;
}
const std::vector<std::string> kDropShape { "drip.ping", "drip.sweep", "drip.fall", "drip.bubble", "drip.wobble", "drip.chirp", "drip.size" };

std::vector<bool> pat16 (const char* s) { std::vector<bool> m; for (; *s; ++s) m.push_back (*s == 'x'); return m; }
std::vector<bool> euclid (int k)
{
    std::vector<bool> m (16);
    for (int i = 0; i < 16; ++i) m[(size_t) i] = i == 0 || (int) std::floor (i * k / 16.0) != (int) std::floor ((i - 1) * k / 16.0);
    return m;
}
const std::map<std::string, std::vector<bool>>& patterns16()
{
    static const std::map<std::string, std::vector<bool>> m {
        { "half", pat16 ("x.......x.......") }, { "offbeats", pat16 ("..x...x...x...x.") }, { "backbeat", pat16 ("....x.......x...") },
        { "tresillo", pat16 ("x..x..x.x..x..x.") }, { "clave32", pat16 ("x..x..x...x.x...") }, { "clave23", pat16 ("..x.x...x..x..x.") },
        { "rumba", pat16 ("x..x...x..x.x...") }, { "bossa", pat16 ("x..x..x...x..x..") }, { "dotted", pat16 ("x..x..x..x..x..x") },
        { "gallop", pat16 ("x.xxx.xxx.xxx.xx") }, { "euclid5", euclid (5) }, { "euclid7", euclid (7) }, { "euclid9", euclid (9) } };
    return m;
}
std::string sectionOf (const std::string& id) { return id.substr (0, id.find ('.')); }
} // namespace

DiceRoller::DiceRoller (const std::vector<std::string>& available, bool isFx, uint32_t seed)
    : T (paramTable()), fx (isFx), rng (seed)
{
    for (auto& s : T)   // table order, like the prototype's POOL
    {
        const std::string id = s.id;
        if (id == "out.level" || id == "drip.edge" || id == "rhy.bpm") continue;   // level, EDGE and tempo stay yours
        if (std::find (available.begin(), available.end(), id) != available.end()) pool.push_back (id);
    }
}

int DiceRoller::index (const std::string& id) const
{
    for (size_t i = 0; i < T.size(); ++i) if (id == T[i].id) return (int) i;
    return -1;
}

void DiceRoller::setConfig (const AutoConfig& c)
{
    const bool rhythmChanged = c.mode != cfg.mode || c.customMask != cfg.customMask;
    if (! c.on) pos = -1;
    cfg = c;
    if (rhythmChanged)
    {
        mask.clear();
        if (cfg.mode == "custom") for (char ch : cfg.customMask) mask.push_back (ch == '1');
    }
}

bool DiceRoller::isFree (const std::string& id) const
{
    if (std::find (pool.begin(), pool.end(), id) == pool.end()) return false;
    if (cfg.holdIds.count (id) || cfg.holdSecs.count (sectionOf (id))) return false;
    if (id == "rhy.rate" && cfg.holdRate) return false;
    if (id == "drip.pattern" && cfg.holdPattern) return false;
    return true;
}

static double normOf (const ParamSpec& p, double v) { return p.log ? std::log (v / p.min) / std::log ((double) p.max / p.min) : (v - p.min) / ((double) p.max - p.min); }
static double fromNormOf (const ParamSpec& p, double t) { return p.log ? p.min * std::pow ((double) p.max / p.min, t) : p.min + ((double) p.max - p.min) * t; }

void DiceRoller::rollParam (const std::string& id, bool cursed, std::vector<float>& v)
{
    const int i = index (id); if (i < 0) return;
    const auto& p = T[(size_t) i];
    if (p.kind == Kind::choice)
    {
        const auto& sc = safeChoices();
        const auto it = sc.find (id);
        if (cursed || it == sc.end()) v[(size_t) i] = (float) (int) (rnd() * (double) p.keys.size());
        else v[(size_t) i] = (float) it->second[(size_t) (rnd() * (double) it->second.size())];
        return;
    }
    double lo = p.min, hi = p.max;
    if (! cursed) { const auto it = safeRanges().find (id); if (it != safeRanges().end()) { lo = it->second.first; hi = it->second.second; } }
    const double a = normOf (p, lo), b = normOf (p, hi);
    double x = fromNormOf (p, a + (b - a) * rnd());
    if (p.kind == Kind::integer) x = std::round (x);
    v[(size_t) i] = (float) std::min ((double) p.max, std::max ((double) p.min, x));
}

void DiceRoller::nudge (const std::string& id, double amt, std::vector<float>& v)
{
    const int i = index (id); if (i < 0) return;
    const auto& p = T[(size_t) i];
    if (p.kind != Kind::number) return;   // choices and stepped dials aren't nudged
    double lo = p.min, hi = p.max;
    { const auto it = safeRanges().find (id); if (it != safeRanges().end()) { lo = it->second.first; hi = it->second.second; } }
    const double cur = normOf (p, v[(size_t) i]);
    const double t = cur + (rnd() * 2 - 1) * amt;
    const double a = std::min (normOf (p, lo), cur), b = std::max (normOf (p, hi), cur);
    v[(size_t) i] = (float) fromNormOf (p, std::min (b, std::max (a, t)));
}

RollNotice DiceRoller::roll (const std::string& key, const Getter& get, const Setter& set)
{
    std::vector<float> v (T.size(), 0.0f), before;
    for (auto& id : pool) v[(size_t) index (id)] = get (id);
    before = v;

    std::vector<std::string> free;
    for (auto& id : pool) if (isFree (id)) free.push_back (id);
    auto shuffled = [this] (std::vector<std::string> a)
    {
        for (int i = (int) a.size() - 1; i > 0; --i) { const int j = (int) (rnd() * (i + 1)); std::swap (a[(size_t) i], a[(size_t) j]); }
        return a;
    };
    auto setIfFree = [&] (const char* id, float val) { if (isFree (id)) v[(size_t) index (id)] = val; };
    const float lp24OrBp[] = { 2.0f, 4.0f };

    RollNotice notice;
    if (key == "all")
    {
        static const int sides[] = { 4, 6, 8, 12, 20 };
        int nums[5], total = 0;
        for (int i = 0; i < 5; ++i) { nums[i] = 1 + (int) (rnd() * sides[i]); total += nums[i]; }
        notice = { "SET", total };
        if (! free.empty())
        {
            if (total == 5) for (auto& id : free) nudge (id, 0.015, v);
            else
            {
                const double share = total / 50.0;
                const int count = std::max (1, (int) std::lround (share * (double) free.size()));
                auto re = shuffled (free); re.resize ((size_t) std::min ((int) re.size(), count));
                std::set<std::string> reSet (re.begin(), re.end());
                for (auto& id : re) rollParam (id, false, v);
                for (auto& id : free) if (! reSet.count (id)) nudge (id, nums[0] * 0.02, v);
                if (total == 50) { setIfFree ("filt.type", lp24OrBp[(int) (rnd() * 2)]); setIfFree ("drip.width", (float) (0.85 + rnd() * 0.15)); }
            }
        }
    }
    else
    {
        const int sides = key == "d4" ? 4 : key == "d6" ? 6 : key == "d8" ? 8 : key == "d12" ? 12 : 20;
        const int n = 1 + (int) (rnd() * sides);
        notice = { key == "cursed" ? "d20" : key, n };
        if (! free.empty())
        {
            if (key == "d4") for (auto& id : free) nudge (id, n * 0.025, v);
            else if (key == "d6")
            {
                std::vector<std::string> shape; for (auto& id : kDropShape) if (isFree (id)) shape.push_back (id);
                shape = shuffled (shape); if ((int) shape.size() > n) shape.resize ((size_t) n);
                for (auto& id : shape) rollParam (id, false, v);
            }
            else if (key == "d8")
            {
                std::vector<std::string> ids { "drip.pattern", "drip.density", "drip.width" };
                if (fx) ids.push_back ("rhy.rate");
                if (n >= 5) ids.push_back ("drip.cutoff");
                if (n == 8) ids.push_back ("drip.res");
                for (auto& id : ids) if (isFree (id)) rollParam (id, false, v);
            }
            else if (key == "d12")
            {
                for (auto& id : free)
                {
                    const auto sec = sectionOf (id);
                    const bool ok = sec == "exc" || sec == "filt" || sec == "amp" || (fx && (sec == "in" || sec == "rhy"));
                    if (ok && (id != "filt.type" || n >= 7)) rollParam (id, false, v);
                }
            }
            else if (key == "d20")
            {
                if (n == 1) for (auto& id : free) nudge (id, 0.02, v);   // fumble
                else
                {
                    for (auto& id : free) rollParam (id, false, v);
                    if (n == 20) { setIfFree ("filt.type", lp24OrBp[(int) (rnd() * 2)]); setIfFree ("drip.width", (float) (0.85 + rnd() * 0.15)); }
                }
            }
            else if (key == "cursed") for (auto& id : free) rollParam (id, true, v);
        }
    }
    for (auto& id : pool)
    {
        const auto i = (size_t) index (id);
        if (v[i] != before[i]) set (id, v[i]);
    }
    notices.push_back (notice);
    return notice;
}

std::vector<bool> DiceRoller::maskFor (const std::string& mode, int len)
{
    const int per = std::max (1, len / 4);   // steps per beat
    std::vector<bool> m ((size_t) len, false);
    if (mode == "random")
    {
        bool any = false;
        for (auto&& b : m) { b = rnd() < 0.35; any = any || b; }
        if (! any) m[0] = true;
        return m;
    }
    const auto& P = patterns16();
    if (const auto it = P.find (mode); it != P.end())
    {
        for (int i = 0; i < 16; ++i) if (it->second[(size_t) i]) m[(size_t) (i * len / 16)] = true;
        return m;
    }
    for (int i = 0; i < len; ++i)
        m[(size_t) i] = mode == "steps" ? true : mode == "8ths" ? i % std::max (1, per / 2) == 0 : mode == "beats" ? i % per == 0 : i == 0;
    return m;
}

void DiceRoller::resizeMask (int len)
{
    if ((int) mask.size() == len) return;
    if (cfg.mode != "custom" || mask.empty()) { mask = maskFor (cfg.mode == "custom" ? "beats" : cfg.mode, len); return; }
    std::vector<bool> m ((size_t) len, false);   // keep your pattern when the grid changes
    for (size_t i = 0; i < mask.size(); ++i) if (mask[i]) m[i * (size_t) len / mask.size()] = true;
    mask = m;
}

void DiceRoller::onGridStep (long long k, const Getter& get, const Setter& set)
{
    if (! haveLast || k < lastStep) { lastStep = k - 1; haveLast = true; }
    if (k - lastStep > 64) lastStep = k - 1;   // after a long pause, don't fire a backlog
    while (lastStep < k)
    {
        ++lastStep;
        if (! cfg.on) continue;
        static const int rates[] = { 1, 2, 4, 8 };
        const int rateIdx = fx ? std::clamp ((int) std::lround (get ("rhy.rate")), 0, 3) : 2;
        const int len = 4 * rates[rateIdx];
        resizeMask (len);
        pos = (int) (((lastStep % len) + len) % len);
        if (pos == 0 && cfg.mode == "random") mask = maskFor ("random", len);   // a new random bar
        if (mask[(size_t) pos]) roll (cfg.die, get, set);
    }
}
} // namespace fluidic
