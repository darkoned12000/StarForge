#include "vulkan/vulkan_swapchain.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Loader-free swapchain selection helpers (M5 Phase 1). These operate only on
// Vulkan value types and enum constants, so they live in starforge_vulkan_core
// and can be used by CPU-only tests without libvulkan at runtime.

namespace starforge::vulkan
{

const char* formatName(VkFormat format) noexcept
{
    switch (format)
    {
    case VK_FORMAT_B8G8R8A8_SRGB:  return "B8G8R8A8_SRGB";
    case VK_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
    case VK_FORMAT_R8G8B8A8_SRGB:  return "R8G8B8A8_SRGB";
    case VK_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
    default:                       return "VkFormat(other)";
    }
}

const char* presentModeName(VkPresentModeKHR mode) noexcept
{
    switch (mode)
    {
    case VK_PRESENT_MODE_IMMEDIATE_KHR:    return "IMMEDIATE";
    case VK_PRESENT_MODE_MAILBOX_KHR:      return "MAILBOX";
    case VK_PRESENT_MODE_FIFO_KHR:         return "FIFO";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO_RELAXED";
    default:                               return "VkPresentModeKHR(other)";
    }
}

std::expected<VkSurfaceFormatKHR, std::string>
chooseSurfaceFormat(std::span<const VkSurfaceFormatKHR> formats)
{
    if (formats.empty())
    {
        return std::unexpected("Surface reports no supported formats");
    }

    for (const auto& fmt : formats)
    {
        if (fmt.format == VK_FORMAT_B8G8R8A8_SRGB && fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return fmt;
        }
    }
    for (const auto& fmt : formats)
    {
        if (fmt.format == VK_FORMAT_R8G8B8A8_SRGB && fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return fmt;
        }
    }

    if (formats[0].format == VK_FORMAT_UNDEFINED)
    {
        return VkSurfaceFormatKHR{
            .format = VK_FORMAT_B8G8R8A8_SRGB,
            .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        };
    }

    return formats[0];
}

VkPresentModeKHR choosePresentMode(std::span<const VkPresentModeKHR> presentModes)
{
    for (const auto& mode : presentModes)
    {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            return mode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

std::expected<VkExtent2D, std::string>
chooseSwapchainExtent(const VkSurfaceCapabilitiesKHR& capabilities,
                      std::uint32_t requestedWidth,
                      std::uint32_t requestedHeight)
{
    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == UINT32_MAX)
    {
        if (requestedWidth == 0 || requestedHeight == 0)
        {
            return std::unexpected("Swapchain extent is zero (window minimized)");
        }
        extent.width = std::clamp(requestedWidth, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height = std::clamp(requestedHeight, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }

    if (extent.width == 0 || extent.height == 0)
    {
        return std::unexpected("Swapchain extent is zero (window minimized)");
    }

    return extent;
}

} // namespace starforge::vulkan
