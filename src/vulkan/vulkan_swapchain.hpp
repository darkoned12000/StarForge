#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace starforge::vulkan
{

// Pure selection helpers (tested in T1 without a GPU or surface).
[[nodiscard]] std::expected<VkSurfaceFormatKHR, std::string>
chooseSurfaceFormat(std::span<const VkSurfaceFormatKHR> formats);

[[nodiscard]] VkPresentModeKHR
choosePresentMode(std::span<const VkPresentModeKHR> presentModes);

[[nodiscard]] std::expected<VkExtent2D, std::string>
chooseSwapchainExtent(const VkSurfaceCapabilitiesKHR& capabilities,
                      std::uint32_t requestedWidth,
                      std::uint32_t requestedHeight);

[[nodiscard]] const char* formatName(VkFormat format) noexcept;
[[nodiscard]] const char* presentModeName(VkPresentModeKHR mode) noexcept;

class VulkanSwapchain
{
public:
    struct CreateInfo
    {
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        std::uint32_t graphicsQueueFamily = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    [[nodiscard]] static std::expected<VulkanSwapchain, std::string> create(const CreateInfo& info);

    ~VulkanSwapchain();
    VulkanSwapchain(VulkanSwapchain&& other) noexcept;
    VulkanSwapchain& operator=(VulkanSwapchain&& other) noexcept;
    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    // Recreates the swapchain in-place using `oldSwapchain` after waiting for
    // the device to idle. Returns an error if the new extent is zero (e.g. minimized).
    [[nodiscard]] std::expected<void, std::string> recreate(std::uint32_t width, std::uint32_t height);

    [[nodiscard]] VkSwapchainKHR swapchain() const noexcept { return swapchain_; }
    [[nodiscard]] VkFormat format() const noexcept { return format_; }
    [[nodiscard]] VkExtent2D extent() const noexcept { return extent_; }
    [[nodiscard]] const std::vector<VkImage>& images() const noexcept { return images_; }
    [[nodiscard]] const std::vector<VkImageView>& imageViews() const noexcept { return imageViews_; }

private:
    VulkanSwapchain() = default;
    void destroy() noexcept;

    [[nodiscard]] static std::expected<VulkanSwapchain, std::string>
    build(const CreateInfo& info, VkSwapchainKHR oldSwapchain);

    VkDevice device_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_B8G8R8A8_SRGB;
    VkColorSpaceKHR colorSpace_ = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkPresentModeKHR presentMode_ = VK_PRESENT_MODE_FIFO_KHR;
    VkExtent2D extent_ = {0, 0};
    std::vector<VkImage> images_;
    std::vector<VkImageView> imageViews_;
    CreateInfo info_;
};

} // namespace starforge::vulkan
