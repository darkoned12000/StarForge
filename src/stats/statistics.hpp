#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace starforge::stats
{

// Summary statistics computed from a sample set.
//
// Percentile method: linear interpolation (NumPy default, R type 7).
//   index = (p/100) * (n-1); interpolate between floor and ceil.
// Standard deviation: sample stddev (divide by n-1, not n).
struct Statistics
{
    std::size_t count = 0;   // number of finite samples used
    bool        valid = false; // false if input was empty or all non-finite

    double min = 0.0;
    double max = 0.0;
    double mean = 0.0;
    double median = 0.0;     // 50th percentile
    double p95 = 0.0;        // 95th percentile
    double p99 = 0.0;        // 99th percentile
    double stddev = 0.0;     // sample standard deviation (n-1 denominator)
};

// Compute statistics from a span of double samples.
// Non-finite values (NaN, +/-Inf) are filtered out. If no finite values
// remain, the result has valid=false and count=0.
[[nodiscard]] Statistics compute(std::span<const double> samples);

// Compute statistics from a span of float samples (converted to double).
[[nodiscard]] Statistics compute(std::span<const float> samples);

// Compute statistics from already-sorted, finite samples. This avoids
// filtering/sorting/allocation overhead. Debug builds assert that data is
// sorted and finite.
[[nodiscard]] Statistics computeSorted(std::span<const double> sortedSamples);

// Compute statistics in place by filtering non-finite values and sorting the
// mutable buffer. This avoids allocating a separate sample buffer.
[[nodiscard]] Statistics computeInPlace(std::span<double> samples);

// Compute a single percentile (0..100) from sorted data.
// Finite values outside [0, 100] are clamped; non-finite p returns NaN.
// Returns NaN if data is empty.
[[nodiscard]] double percentile(std::span<const double> sortedData, double p);

} // namespace starforge::stats
