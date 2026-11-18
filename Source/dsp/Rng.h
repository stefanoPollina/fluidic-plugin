#pragma once
#include <cstdint>

namespace fluidic
{
// mulberry32 — small, fast, and identical to the JavaScript version used in the parity tests,
// so the C++ engine can be checked sample-for-sample against the web prototype.
struct Rng
{
    uint32_t s = 0x9e3779b9u;
    void seed (uint32_t v) noexcept { s = v; }
    double next() noexcept
    {
        s += 0x6D2B79F5u;
        uint32_t t = (s ^ (s >> 15)) * (1u | s);
        t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
        return double (t ^ (t >> 14)) / 4294967296.0;
    }
};
} // namespace fluidic
