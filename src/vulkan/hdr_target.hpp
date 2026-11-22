#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace starforge::vulkan
{

// Offscreen floating-point scene colour targets, one image per frame in
// flight. The scene pass renders into these (linear HDR, values may exceed
// 1.0); a later resolve pass tonemaps them into the swapchain.
//
// Lifetime rules:
//  * Images are created in UNDEFINED layout; FrameRenderer owns all layout
//    transitions.
//  * `count` must be >= FrameRenderer::framesInFlight().
//  * Recreate whenever the swapchain extent changes (same place you recreate
//    the swapchain depth images). The caller must make sure the GPU is idle
//    (or the old frames have retired) before destroying the old target.
//
// NOTE: memory is allocated with plain vkAllocateMemory so this file is
// self-contained. If the project already has an allocator (VMA, a
// VulkanImage helper used for the swapchain depth images) swap
// createOne() over to it; nothing else depends on how the memory is made.
class HdrTarget
{
public:
    struct CreateInfo
    {
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkExtent2D extent{};
        std::uint32_t count = 2;
        VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
        // OR'd into COLOR_ATTACHMENT | SAMPLED (e.g. STORAGE for a compute resolve).
        VkImageUsageFlags extraUsage = 0;
    };

    [[nodiscard]] static std::expected<HdrTarget, std::string> create(const CreateInfo& info);

    HdrTarget() = default;
    ~HdrTarget();
    HdrTarget(HdrTarget&& other) noexcept;
    HdrTarget& operator=(HdrTarget&& other) noexcept;
    HdrTarget(const HdrTarget&) = delete;
    HdrTarget& operator=(const HdrTarget&) = delete;

    [[nodiscard]] bool valid() const noexcept { return device_ != VK_NULL_HANDLE && !images_.empty(); }
    [[nodiscard]] std::uint32_t count() const noexcept { return static_cast<std::uint32_t>(images_.size()); }
    [[nodiscard]] VkFormat format() const noexcept { return format_; }
    [[nodiscard]] VkExtent2D extent() const noexcept { return extent_; }
    [[nodiscard]] VkImage image(std::uint32_t frame) const { return images_.at(frame); }
    [[nodiscard]] VkImageView view(std::uint32_t frame) const { return views_.at(frame); }

private:
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    std::vector<VkImageView> views_;
};

} // namespace starforge::vulkan
