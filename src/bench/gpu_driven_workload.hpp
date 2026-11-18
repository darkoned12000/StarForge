#pragma once

#include "bench/benchmark_harness.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/compute_pipeline.hpp"
#include "gpu_resources/gpu_culling.hpp"
#include "gpu_resources/gpu_driven_draw.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"
#include "vulkan/vulkan_timestamps.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <vector>

namespace starforge::bench
{

struct GpuDrivenWorkloadConfig
{
    std::uint32_t instanceCount = 100000;
    double        visibleFraction = 0.1;
    std::uint32_t framesInFlight = 2;
    std::uint32_t width = 1920;
    std::uint32_t height = 1080;
    std::uint64_t seed = 42;
    std::string   shaderDirectory;
};

// Profile A GPU-driven workload: culls on the GPU and draws the compacted
// instances with a single indirect draw. Owns its per-frame render targets and
// a GpuTimer scope around the culling dispatch (M5 Phase 5).
class GpuDrivenWorkload
{
public:
    static std::expected<GpuDrivenWorkload, std::string>
    create(const vulkan::VulkanContext& context,
           const vulkan::VulkanDevice& device,
           gpu_resources::Allocator& allocator,
           const GpuDrivenWorkloadConfig& config);

    ~GpuDrivenWorkload();
    GpuDrivenWorkload(GpuDrivenWorkload&&) noexcept;
    GpuDrivenWorkload& operator=(GpuDrivenWorkload&&) noexcept;
    GpuDrivenWorkload(const GpuDrivenWorkload&) = delete;
    GpuDrivenWorkload& operator=(const GpuDrivenWorkload&) = delete;

    // Records one frame (cull + indirect draw) into ctx.cmd.
    void record(WorkloadContext& ctx);

    // Median GPU culling duration in milliseconds from collected timestamps.
    [[nodiscard]] double cullDurationMs() const;

private:
    GpuDrivenWorkload() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace starforge::bench
