#pragma once

#include "vulkan/vulkan_pipeline.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace starforge::gpu_resources
{

struct GpuDrivenDrawCreateInfo
{
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    std::uint32_t queueFamily = 0;
    VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::string shaderDirectory;
    bool bindless = true;          // GpuCapabilities.bindless
    bool drawIndirectCount = true; // GpuCapabilities.drawIndirectCount
};

// Records the final indirect draw of GPU-culled instances. Chooses its shader
// and draw command from the capabilities passed at creation, so the fallback
// paths are exercised by STARFORGE_DISABLE_FEATURES (M5 Phase 4).
class GpuDrivenDraw
{
public:
    static std::expected<GpuDrivenDraw, std::string> create(const GpuDrivenDrawCreateInfo& info);

    ~GpuDrivenDraw();
    GpuDrivenDraw(GpuDrivenDraw&& other) noexcept;
    GpuDrivenDraw& operator=(GpuDrivenDraw&& other) noexcept;
    GpuDrivenDraw(const GpuDrivenDraw&) = delete;
    GpuDrivenDraw& operator=(const GpuDrivenDraw&) = delete;

    // Caller must have begun a dynamic-rendering pass with a matching color
    // attachment. Records viewport/scissor, push constants and the draw.
    void record(VkCommandBuffer cmd,
                VkBuffer indirectBuffer,
                VkBuffer boundsBuffer,
                VkBuffer visibleIndicesBuffer,
                const float* viewProj16,
                VkDeviceAddress boundsAddress = 0,
                VkDeviceAddress visibleIndicesAddress = 0) const;

    [[nodiscard]] bool usingDescriptorFallback() const noexcept { return !bindless_; }
    [[nodiscard]] bool usingDrawIndirectFallback() const noexcept { return !drawIndirectCount_; }

    enum class DrawCallKind
    {
        DrawIndirect,
        DrawIndirectCount,
    };
    [[nodiscard]] DrawCallKind lastDrawCallKind() const noexcept { return lastDrawCallKind_; }
    [[nodiscard]] std::uint64_t descriptorUpdateCount() const noexcept { return descriptorUpdateCount_; }

private:
    GpuDrivenDraw() = default;
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    bool bindless_ = true;
    bool drawIndirectCount_ = true;

    VkShaderModule vertexModule_ = VK_NULL_HANDLE;
    VkShaderModule fragmentModule_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    std::optional<vulkan::GraphicsPipeline> pipeline_;

    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    mutable VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    mutable VkBuffer boundBounds_ = VK_NULL_HANDLE;
    mutable VkBuffer boundIndices_ = VK_NULL_HANDLE;
    mutable DrawCallKind lastDrawCallKind_ = DrawCallKind::DrawIndirect;
    mutable std::uint64_t descriptorUpdateCount_ = 0;
};

} // namespace starforge::gpu_resources
