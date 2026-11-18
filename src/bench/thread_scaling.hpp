#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace starforge::bench
{

// Configuration for the M5.5 CPU scaling proof (1/2/4/8/16 threads).
struct ThreadScalingConfig
{
    std::vector<std::uint32_t> threadCounts{1, 2, 4, 8, 16};
    std::uint64_t seed = 42;
    std::size_t   batchCount = 24;     // parallel content batches per run
    std::size_t   cellsPerBatch = 32;  // cells per batch
    int           level = 0;
    std::size_t   heightSamples = 20000; // terrain height evaluations per run
    std::uint32_t repeats = 3;         // timed runs per thread count (median used)
};

struct ThreadScalingSample
{
    std::uint32_t threadCount = 0;
    double        medianMs = 0.0;
    double        bestMs = 0.0;
    double        speedup = 1.0;       // baseline median / this median
    std::uint64_t checksum = 0;        // bit-exact workload result
    bool          bitIdentical = false;
};

struct ThreadScalingResult
{
    std::uint32_t schemaVersion = 1;
    ThreadScalingConfig config;
    std::string         cpuModel;
    std::uint32_t       cpuCoreCount = 0;
    std::string         generatedAt;
    std::vector<ThreadScalingSample> samples;

    [[nodiscard]] std::string toJson() const;
    [[nodiscard]] static std::expected<ThreadScalingResult, std::string> fromJson(const std::string& json);
};

// Runs the deterministic content + terrain workload at each configured thread
// count. All counts must produce the same bit-exact checksum, otherwise the
// result is invalid (parallelism must not change generated content).
[[nodiscard]] std::expected<ThreadScalingResult, std::string>
runThreadScalingBenchmark(const ThreadScalingConfig& config = {});

} // namespace starforge::bench
