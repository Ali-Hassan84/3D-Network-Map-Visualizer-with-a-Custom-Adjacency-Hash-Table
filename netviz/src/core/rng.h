// rng.h - small deterministic PRNG (xorshift64*). Same seed => same sequence on every platform,
// which is what the "fixed seeds for strict reproducibility" requirement needs.
#pragma once
#include <cstdint>

struct Rng { uint64_t state; };

inline void RngSeed(Rng& r, uint64_t seed) {
    // splitmix64 scramble so that small seeds (1, 2, 3...) give unrelated streams
    uint64_t z = seed + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z ^= (z >> 31);
    r.state = z ? z : 0x9E3779B97F4A7C15ULL;
}
inline uint64_t RngNext(Rng& r) {
    uint64_t x = r.state;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    r.state = x;
    return x * 2685821657736338717ULL;
}
inline double   RngDouble(Rng& r)           { return (double)(RngNext(r) >> 11) * (1.0 / 9007199254740992.0); }
inline uint32_t RngInt(Rng& r, uint32_t n)  { return n ? (uint32_t)((RngNext(r) >> 32) % n) : 0u; }
