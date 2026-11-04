#pragma once

#include "bench/benchmark_result.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace starforge::bench
{

// Result of comparing two benchmark results.
struct CompareResult
{
    struct MetricComparison
    {
        std::string metricName;
        double      baselineValue = 0.0; // median
        double      candidateValue = 0.0; // median
        double      ratio = 1.0;        // candidate / baseline
        double      percentChange = 0.0; // (candidate - baseline) / baseline * 100
        bool        isRegression = false;
        bool        isImprovement = false;
    };

    std::vector<MetricComparison> comparisons;
    bool hasIncompatibleWorkload = false;
    std::string incompatibilityReason;
    bool hasGpuMismatch = false;
    std::string gpuMismatchReason;
    bool hasBuildMismatch = false;
    std::string buildMismatchReason;
    bool hasRegression = false;
    double noiseThresholdPercent = 5.0; // default 5%
};

// Compare two benchmark results.
[[nodiscard]] CompareResult compare(const BenchmarkResult& baseline, const BenchmarkResult& candidate);

// Check if two workloads are compatible for comparison.
[[nodiscard]] std::expected<void, std::string> checkCompatibility(const BenchmarkResult& baseline, const BenchmarkResult& candidate);

} // namespace starforge::bench
