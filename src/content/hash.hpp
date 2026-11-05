#pragma once

#include <cstdint>

namespace starforge::content
{

// SplitMix64 — high-quality 64-bit hash mixer.
[[nodiscard]] inline std::uint64_t splitMix64(std::uint64_t x) noexcept
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111b9ULL;
    return x ^ (x >> 31);
}

// Hash a cell coordinate triple into a 64-bit seed.
// Pure integer, no floating-point. Deterministic across platforms.
[[nodiscard]] inline std::uint64_t hashCell(
    std::int64_t cx, std::int64_t cy, std::int64_t cz) noexcept
{
    std::uint64_t h = 0xcbf29ce484222325ULL; // FNV offset basis
    auto mix = [&h](std::int64_t v) {
        std::uint64_t u = static_cast<std::uint64_t>(v);
        for (int i = 0; i < 8; ++i)
        {
            h ^= (u & 0xFFULL);
            h *= 0x100000001b3ULL; // FNV prime
            u >>= 8;
        }
    };
    mix(cx);
    mix(cy);
    mix(cz);
    return splitMix64(h);
}

// Hash a cell + level + world seed into a content seed.
[[nodiscard]] inline std::uint64_t contentSeed(
    std::uint64_t worldSeed,
    std::int64_t cx, std::int64_t cy, std::int64_t cz,
    int level) noexcept
{
    std::uint64_t h = worldSeed;
    h ^= hashCell(cx, cy, cz);
    h = splitMix64(h + static_cast<std::uint64_t>(level) * 0x9e3779b97f4a7c15ULL);
    return h;
}

// Deterministic pseudo-random number generator (xorshift64).
struct DeterministicRng
{
    std::uint64_t state;

    explicit DeterministicRng(std::uint64_t seed) : state(seed ? seed : 1) {}

    std::uint64_t next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }

    // Returns a float in [0, 1).
    float nextFloat() noexcept
    {
        return static_cast<float>(next() >> 40) * (1.0f / 16777216.0f); // 2^24
    }

    // Returns a double in [0, 1).
    double nextDouble() noexcept
    {
        // 53-bit mantissa for exact double representation.
        return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0);
    }

    // Returns an int in [min, max] using rejection sampling (no modulo bias).
    int nextInt(int min, int max) noexcept
    {
        if (max <= min) return min;

        const std::uint64_t range =
            static_cast<std::uint64_t>(static_cast<std::int64_t>(max) - min) + 1ULL;
        // Reject the low tail so the remaining range is a multiple of `range`.
        const std::uint64_t threshold = (0ULL - range) % range;
        std::uint64_t r = next();
        while (r < threshold)
        {
            r = next();
        }
        return static_cast<int>(static_cast<std::int64_t>(min)
                                + static_cast<std::int64_t>(r % range));
    }
};

} // namespace starforge::content