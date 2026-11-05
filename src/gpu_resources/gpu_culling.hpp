#pragma once

#include "gpu_resources/allocator.hpp"
#include "gpu_resources/buffer.hpp"
#include "gpu_resources/compute_pipeline.hpp"
#include "vulkan/vulkan_device.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace starforge::gpu_resources
{

// Manages GPU-driven culling resources: instance bounds (SoA), indirect
// command buffers (double-buffered per frame in flight), and compacted
// visible instance index arrays (double-buffered).
class GpuCulling
{
public:
    struct CreateInfo
    {
        std::uint32_t instanceCount = 0;
        std::uint32_t framesInFlight = 2;
        const char* name = nullptr;
    };

    [[nodiscard]] static std::expected<GpuCulling, std::string>
    create(Allocator& allocator, const CreateInfo& info);

    ~GpuCulling();
    GpuCulling(GpuCulling&& other) noexcept;
    GpuCulling& operator=(GpuCulling&& other) noexcept;
    GpuCulling(const GpuCulling&) = delete;
    GpuCulling& operator=(const GpuCulling&) = delete;

    // Upload instance bounds (vec4 per instance: center XYZ + radius W).
    [[nodiscard]] std::expected<void, std::string>
    uploadBounds(const std::vector<float>& boundsData);

    // Get the indirect command buffer for a given frame slot.
    [[nodiscard]] Buffer& indirectBuffer(std::uint32_t frameIndex) noexcept;
    [[nodiscard]] const Buffer& indirectBuffer(std::uint32_t frameIndex) const noexcept;

    // Get the visible indices buffer for a given frame slot.
    [[nodiscard]] Buffer& visibleIndicesBuffer(std::uint32_t frameIndex) noexcept;
    [[nodiscard]] const Buffer& visibleIndicesBuffer(std::uint32_t frameIndex) const noexcept;

    // Get the bounds buffer (single, shared across frames).
    [[nodiscard]] Buffer& boundsBuffer() noexcept;
    [[nodiscard]] const Buffer& boundsBuffer() const noexcept;

    [[nodiscard]] std::uint32_t instanceCount() const noexcept { return instanceCount_; }
    [[nodiscard]] std::uint32_t framesInFlight() const noexcept { return framesInFlight_; }

    // Reset the indirect command buffer for a frame slot (zero instanceCount).
    [[nodiscard]] std::expected<void, std::string>
    resetIndirectBuffer(std::uint32_t frameIndex);

    // Record the full GPU culling dispatch sequence into a command buffer.
    // Steps: reset indirect buffer → barrier → bind pipeline + push constants →
    // dispatch → barrier (compute write → indirect read + vertex read).
    [[nodiscard]] std::expected<void, std::string>
    recordCulling(VkCommandBuffer cmd,
                  VkDevice device,
                  ComputePipeline& cullPipeline,
                  std::uint32_t frameIndex,
                  const float* frustumPlanes, // 24 floats (6 planes × 4)
                  std::uint32_t totalInstanceCount);

private:
    GpuCulling() = default;
    void destroy() noexcept;

    std::uint32_t instanceCount_ = 0;
    std::uint32_t framesInFlight_ = 2;

    // Single bounds buffer (shared, read-only during culling).
    Buffer boundsBuffer_;

    // Double-buffered per frame.
    std::vector<Buffer> indirectBuffers_;
    std::vector<Buffer> visibleIndicesBuffers_;
};

} // namespace starforge::gpu_resources