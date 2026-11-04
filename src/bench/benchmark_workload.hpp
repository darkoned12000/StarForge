#pragma once

#include "bench/benchmark_scene.hpp"
#include "bench/benchmark_result.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace starforge::bench
{

// Configuration for a benchmark workload.
struct WorkloadConfig
{
    std::uint32_t instanceCount = 100000;
    double        radius = 10.0;        // scene radius in metres
    double        visibleFraction = 1.0; // target visible fraction
    bool          enableCulling = false;  // CPU frustum culling
    bool          useInstancing = false;  // instanced draws (single draw call)
    std::uint64_t seed = 42;
    std::uint32_t resolutionWidth = 1920;
    std::uint32_t resolutionHeight = 1080;
};

// Result of running a workload.
struct WorkloadResult
{
    std::uint32_t instancesSubmitted = 0;
    std::uint32_t instancesVisible = 0;
    std::uint32_t drawCalls = 0;
};

// Generate a scene and camera for a workload config.
[[nodiscard]] BenchmarkScene generateWorkloadScene(const WorkloadConfig& config);

// Build a camera frustum for the workload.
[[nodiscard]] Frustum buildWorkloadFrustum(const WorkloadConfig& config);

// Run a single frame of the workload, recording commands into cmd.
// Returns the number of instances submitted and draw calls.
[[nodiscard]] WorkloadResult recordWorkloadFrame(
    const WorkloadConfig& config,
    const BenchmarkScene& scene,
    const Frustum& frustum,
    VkCommandBuffer cmd,
    VkPipelineLayout pipelineLayout,
    VkPipeline pipeline);

} // namespace starforge::bench
