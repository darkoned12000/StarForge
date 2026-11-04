#pragma once

#include "bench/benchmark_result.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"
#include "vulkan/vulkan_timestamps.hpp"

#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <vector>

namespace starforge::bench
{

// Configuration for a benchmark run.
struct HarnessConfig
{
    std::uint32_t warmupFrames = 10;
    std::uint32_t measuredFrames = 100;
    std::string   timingMode = "throughput"; // "latency" or "throughput"
    std::uint32_t framesInFlight = 2;
    bool          requireRelease = true;
    bool          requireValidationOff = true;
    std::string   benchmarkId = "unnamed";
    std::string   workloadId = "unnamed";
    std::uint64_t seed = 42;
};

// Context passed to the workload callback for each frame.
struct WorkloadContext
{
    VkDevice device = VK_NULL_HANDLE;
    VkQueue  queue = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    std::uint32_t frameIndex = 0;
    std::uint32_t framesInFlight = 2;
};

// Workload callback: records commands into cmd for one frame.
using WorkloadFn = std::function<void(WorkloadContext&)>;

// Benchmark harness: runs a workload headlessly and produces a BenchmarkResult.
class BenchmarkHarness
{
public:
    // Run a benchmark. Returns an error if prerequisites aren't met.
    [[nodiscard]] static std::expected<BenchmarkResult, std::string>
    run(const HarnessConfig& config,
        const WorkloadFn& workload,
        const vulkan::VulkanContext& context,
        const vulkan::VulkanDevice& device);

    // Check if the current build meets benchmark prerequisites.
    [[nodiscard]] static std::expected<void, std::string>
    checkPrerequisites(const HarnessConfig& config);

    // Collect system metadata for the benchmark result.
    [[nodiscard]] static GpuInfo collectGpuInfo(const vulkan::VulkanContext& context);
    [[nodiscard]] static CpuInfo collectCpuInfo();
    [[nodiscard]] static SystemInfo collectSystemInfo();
    [[nodiscard]] static BuildInfo collectBuildInfo();
    [[nodiscard]] static EnvironmentInfo collectEnvironmentInfo();
};

} // namespace starforge::bench
