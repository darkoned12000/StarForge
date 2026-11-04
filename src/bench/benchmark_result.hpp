#pragma once

#include "stats/statistics.hpp"

#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace starforge::bench
{

// Versioned schema for benchmark results.
inline constexpr std::uint32_t kSchemaVersion = 1;

// Source control metadata.
struct SourceInfo
{
    std::string commitHash;
    bool        dirty = false;
    std::optional<std::string> diffHash;
};

// GPU metadata.
struct GpuInfo
{
    std::string deviceName;
    std::uint32_t vendorId = 0;
    std::string driverVersion;
    std::string vulkanApiVersion;
    std::uint32_t timestampValidBits = 0;
    double        timestampPeriodNs = 0.0;
};

// CPU metadata.
struct CpuInfo
{
    std::string model;
    std::uint32_t coreCount = 0;
    std::uint32_t threadCount = 0;
    std::string governor;
};

// System metadata.
struct SystemInfo
{
    std::string os;
    std::string kernel;
    std::uint64_t ramBytes = 0;
};

// Build metadata.
struct BuildInfo
{
    std::string compiler;
    std::string buildType;
    std::string flags;
    bool        validationEnabled = false;
};

// Workload metadata.
struct WorkloadInfo
{
    std::string sceneId;
    std::uint64_t seed = 0;
    std::uint64_t instanceCount = 0;
    double        visibleFraction = 1.0;
    std::uint32_t resolutionWidth = 0;
    std::uint32_t resolutionHeight = 0;
    std::string   meshComplexity;
    std::uint32_t materialCount = 0;
};

// Environment metadata.
struct EnvironmentInfo
{
    std::optional<double> gpuClockMhz;
    std::optional<double> gpuTempC;
    std::optional<double> gpuPowerW;
    std::optional<double> cpuFreqMhz;
    std::optional<double> cpuTempC;
};

// Memory snapshot.
struct MemorySnapshot
{
    std::uint64_t allocatedBytes = 0;
    std::uint64_t usedBytes = 0;
    std::uint64_t allocationCount = 0;
    std::uint64_t blockCount = 0;
};

// A named metric with summary statistics and raw samples.
struct Metric
{
    stats::Statistics summary;
    std::vector<double> rawSamples;
};

// Complete benchmark result.
struct BenchmarkResult
{
    std::uint32_t schemaVersion = kSchemaVersion;
    std::string benchmarkId;
    std::string workloadId;
    std::string timingMode; // "latency" or "throughput"

    SourceInfo source;
    GpuInfo gpu;
    CpuInfo cpu;
    SystemInfo system;
    BuildInfo build;
    WorkloadInfo workload;
    EnvironmentInfo environment;

    std::map<std::string, Metric> metrics;
    MemorySnapshot memory;

    // Serialize to JSON string.
    [[nodiscard]] std::string toJson() const;

    // Deserialize from JSON string.
    [[nodiscard]] static std::expected<BenchmarkResult, std::string>
    fromJson(const std::string& json);
};

} // namespace starforge::bench
