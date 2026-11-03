#include "vulkan/vulkan_swapchain.hpp"

#include "core/log.hpp"
#include "vulkan/vulkan_result.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace starforge::vulkan
{

namespace
{
constexpr const char* kTag = "VULKAN";
}

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

std::expected<VulkanSwapchain, std::string> VulkanSwapchain::create(const CreateInfo& info)
{
    return build(info, VK_NULL_HANDLE);
}

std::expected<VulkanSwapchain, std::string>
VulkanSwapchain::build(const CreateInfo& info, VkSwapchainKHR oldSwapchain)
{
    if (info.device == VK_NULL_HANDLE || info.surface == VK_NULL_HANDLE || info.physicalDevice == VK_NULL_HANDLE)
    {
        return std::unexpected("Invalid swapchain create info");
    }

    VkBool32 presentSupported = VK_FALSE;
    if (vkGetPhysicalDeviceSurfaceSupportKHR(info.physicalDevice, info.graphicsQueueFamily, info.surface, &presentSupported) != VK_SUCCESS ||
        presentSupported == VK_FALSE)
    {
        return std::unexpected("Selected graphics queue family does not support presentation to surface");
    }

    VkSurfaceCapabilitiesKHR capabilities{};
    if (const VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(info.physicalDevice, info.surface, &capabilities);
        result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", result));
    }

    std::uint32_t formatCount = 0;
    if (const VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(info.physicalDevice, info.surface, &formatCount, nullptr);
        result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkGetPhysicalDeviceSurfaceFormatsKHR", result));
    }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    if (formatCount > 0)
    {
        vkGetPhysicalDeviceSurfaceFormatsKHR(info.physicalDevice, info.surface, &formatCount, formats.data());
    }

    const auto surfaceFormat = chooseSurfaceFormat(formats);
    if (!surfaceFormat)
    {
        return std::unexpected(surfaceFormat.error());
    }

    std::uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(info.physicalDevice, info.surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    if (presentModeCount > 0)
    {
        vkGetPhysicalDeviceSurfacePresentModesKHR(info.physicalDevice, info.surface, &presentModeCount, presentModes.data());
    }

    const VkPresentModeKHR presentMode = choosePresentMode(presentModes);

    const auto extent = chooseSwapchainExtent(capabilities, info.width, info.height);
    if (!extent)
    {
        return std::unexpected(extent.error());
    }

    std::uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
    {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = info.surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat->format;
    createInfo.imageColorSpace = surfaceFormat->colorSpace;
    createInfo.imageExtent = *extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = oldSwapchain;

    VulkanSwapchain swapchain;
    swapchain.device_ = info.device;
    swapchain.format_ = surfaceFormat->format;
    swapchain.colorSpace_ = surfaceFormat->colorSpace;
    swapchain.presentMode_ = presentMode;
    swapchain.extent_ = *extent;
    swapchain.info_ = info;

    if (const VkResult result = vkCreateSwapchainKHR(info.device, &createInfo, nullptr, &swapchain.swapchain_);
        result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkCreateSwapchainKHR", result));
    }

    vkGetSwapchainImagesKHR(info.device, swapchain.swapchain_, &imageCount, nullptr);
    swapchain.images_.resize(imageCount);
    vkGetSwapchainImagesKHR(info.device, swapchain.swapchain_, &imageCount, swapchain.images_.data());

    swapchain.imageViews_.resize(imageCount);
    for (std::uint32_t i = 0; i < imageCount; ++i)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = swapchain.images_[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = swapchain.format_;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (const VkResult result = vkCreateImageView(info.device, &viewInfo, nullptr, &swapchain.imageViews_[i]);
            result != VK_SUCCESS)
        {
            return std::unexpected(starforge::vulkan::describeFailure("vkCreateImageView", result));
        }
    }

    core::logInfo(kTag, "Swapchain created: {}x{}, format {}, present mode {}, {} images",
                  extent->width, extent->height, formatName(surfaceFormat->format), presentModeName(presentMode), imageCount);
    return swapchain;
}

std::expected<void, std::string> VulkanSwapchain::recreate(std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0)
    {
        return std::unexpected("Swapchain extent cannot be zero");
    }
    if (device_ == VK_NULL_HANDLE)
    {
        return std::unexpected("Swapchain not initialized");
    }

    vkDeviceWaitIdle(device_);

    CreateInfo newInfo = info_;
    newInfo.width = width;
    newInfo.height = height;

    auto replacement = build(newInfo, swapchain_);
    if (!replacement)
    {
        return std::unexpected(std::move(replacement.error()));
    }

    destroy();
    *this = std::move(*replacement);
    return {};
}

VulkanSwapchain::~VulkanSwapchain()
{
    destroy();
}

VulkanSwapchain::VulkanSwapchain(VulkanSwapchain&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      swapchain_(std::exchange(other.swapchain_, VK_NULL_HANDLE)),
      format_(other.format_),
      colorSpace_(other.colorSpace_),
      presentMode_(other.presentMode_),
      extent_(other.extent_),
      images_(std::move(other.images_)),
      imageViews_(std::move(other.imageViews_)),
      info_(other.info_)
{
}

VulkanSwapchain& VulkanSwapchain::operator=(VulkanSwapchain&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        swapchain_ = std::exchange(other.swapchain_, VK_NULL_HANDLE);
        format_ = other.format_;
        colorSpace_ = other.colorSpace_;
        presentMode_ = other.presentMode_;
        extent_ = other.extent_;
        images_ = std::move(other.images_);
        imageViews_ = std::move(other.imageViews_);
        info_ = other.info_;
    }
    return *this;
}

void VulkanSwapchain::destroy() noexcept
{
    if (device_ != VK_NULL_HANDLE)
    {
        for (auto view : imageViews_)
        {
            if (view != VK_NULL_HANDLE)
            {
                vkDestroyImageView(device_, view, nullptr);
            }
        }
        imageViews_.clear();
        images_.clear();
        if (swapchain_ != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
        }
    }
    device_ = VK_NULL_HANDLE;
}

} // namespace starforge::vulkan
