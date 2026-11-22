#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace starforge::vulkan
{

class VulkanContext;

// Logical device and queue access for headless GPU work. Move-only; resources
// are released in the destructor. The device is created without a window or
// surface.
class VulkanDevice
{
public:
    struct CreateInfo
    {
        // Extensions to enable on the device (e.g. VK_KHR_swapchain when needed).
        // The required Vulkan 1.3 features are enabled automatically.
        std::vector<const char*> requiredExtensions;

        // Optional surface. When non-null, the selected queue family must also
        // support presentation to this surface.
        VkSurfaceKHR surface = VK_NULL_HANDLE;
    };

    [[nodiscard]] static std::expected<VulkanDevice, std::string>
    create(const VulkanContext& context);

    [[nodiscard]] static std::expected<VulkanDevice, std::string>
    create(const VulkanContext& context, const CreateInfo& info);

    ~VulkanDevice();
    VulkanDevice(VulkanDevice&& other) noexcept;
    VulkanDevice& operator=(VulkanDevice&& other) noexcept;
    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    [[nodiscard]] VkDevice device() const noexcept { return device_; }
    [[nodiscard]] VkQueue graphicsQueue() const noexcept { return graphicsQueue_; }
    [[nodiscard]] std::uint32_t graphicsQueueFamily() const noexcept { return graphicsQueueFamily_; }

    // Synchronized submission: Vulkan requires host synchronization for
    // vkQueueSubmit2. Background loaders and the render thread can share the
    // same queue safely by routing submissions through this method (M5 2.2).
    [[nodiscard]] VkResult submit(std::uint32_t submitCount,
                                  const VkSubmitInfo2* submits,
                                  VkFence fence) const;

private:
    VulkanDevice() = default;
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue  graphicsQueue_ = VK_NULL_HANDLE;
    std::uint32_t graphicsQueueFamily_ = 0;
};

} // namespace starforge::vulkan
