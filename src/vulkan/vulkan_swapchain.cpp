#include "vulkan/vulkan_swapchain.hpp"

#include "vulkan/vulkan_retirement.hpp"

#include "core/log.hpp"
#include "vulkan/vulkan_result.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace starforge::vulkan
{

namespace
{
constexpr const char* kTag = "VULKAN";

void setDebugObjectName(VkDevice device, std::uint64_t handle, VkObjectType type, const char* name)
{
    if (device == VK_NULL_HANDLE || handle == 0 || name == nullptr || name[0] == '\0')
    {
        return;
    }
    const auto setName = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
        vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT"));
    if (setName == nullptr)
    {
        return;
    }
    VkDebugUtilsObjectNameInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name;
    (void)setName(device, &info);
}
}

// The pure selection helpers (formatName, presentModeName,
// chooseSurfaceFormat, choosePresentMode, chooseSwapchainExtent) live in
// vulkan_swapchain_selection.cpp in starforge_vulkan_core so CPU-only tests
// can use them without linking the Vulkan loader (M5 Phase 1).

std::expected<void, std::string> VulkanSwapchain::createDepthImages(std::uint32_t imageCount)
{
    // Depth format preference mirrors gpu_resources::findDepthFormat
    // (kept local: this library must not depend on gpu_resources).
    const VkFormat candidates[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT};
    depthFormat_ = VK_FORMAT_UNDEFINED;
    for (VkFormat candidate : candidates)
    {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(info_.physicalDevice, candidate, &props);
        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
        {
            depthFormat_ = candidate;
            break;
        }
    }
    if (depthFormat_ == VK_FORMAT_UNDEFINED)
    {
        return std::unexpected("No supported depth format found for swapchain depth images");
    }

    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(info_.physicalDevice, &memProps);
    auto findMemoryType = [&memProps](std::uint32_t typeFilter, VkMemoryPropertyFlags flags) {
        for (std::uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        {
            if ((typeFilter & (1u << i)) != 0 && (memProps.memoryTypes[i].propertyFlags & flags) == flags)
            {
                return i;
            }
        }
        return UINT32_MAX;
    };

    depthImages_.assign(imageCount, VK_NULL_HANDLE);
    depthViews_.assign(imageCount, VK_NULL_HANDLE);
    depthMemory_ = VK_NULL_HANDLE;

    std::vector<VkDeviceSize> offsets(imageCount, 0);
    VkDeviceSize totalSize = 0;
    std::uint32_t memoryTypeBits = 0xFFFFFFFFu;

    for (std::uint32_t i = 0; i < imageCount; ++i)
    {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = depthFormat_;
        imageInfo.extent = {extent_.width, extent_.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (const VkResult result = vkCreateImage(device_, &imageInfo, nullptr, &depthImages_[i]);
            result != VK_SUCCESS)
        {
            return std::unexpected(starforge::vulkan::describeFailure("vkCreateImage (depth)", result));
        }

        VkMemoryRequirements memReq{};
        vkGetImageMemoryRequirements(device_, depthImages_[i], &memReq);
        memoryTypeBits &= memReq.memoryTypeBits;
        const VkDeviceSize align = memReq.alignment == 0 ? 1 : memReq.alignment;
        totalSize = (totalSize + align - 1) & ~(align - 1);
        offsets[i] = totalSize;
        totalSize += memReq.size;
    }

    if (memoryTypeBits == 0)
    {
        return std::unexpected("No common memory type for swapchain depth images");
    }
    const std::uint32_t memType = findMemoryType(memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memType == UINT32_MAX)
    {
        return std::unexpected("No device-local memory for swapchain depth images");
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = totalSize;
    allocInfo.memoryTypeIndex = memType;
    if (const VkResult result = vkAllocateMemory(device_, &allocInfo, nullptr, &depthMemory_);
        result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkAllocateMemory (depth)", result));
    }
    setDebugObjectName(device_, reinterpret_cast<std::uint64_t>(depthMemory_), VK_OBJECT_TYPE_DEVICE_MEMORY, "starforge.swapchain.depth_memory");

    const VkImageAspectFlags aspect =
        (depthFormat_ == VK_FORMAT_D24_UNORM_S8_UINT || depthFormat_ == VK_FORMAT_D32_SFLOAT_S8_UINT)
        ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
        : VK_IMAGE_ASPECT_DEPTH_BIT;

    for (std::uint32_t i = 0; i < imageCount; ++i)
    {
        if (const VkResult result = vkBindImageMemory(device_, depthImages_[i], depthMemory_, offsets[i]);
            result != VK_SUCCESS)
        {
            return std::unexpected(starforge::vulkan::describeFailure("vkBindImageMemory (depth)", result));
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = depthImages_[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = depthFormat_;
        viewInfo.subresourceRange.aspectMask = aspect;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        if (const VkResult result = vkCreateImageView(device_, &viewInfo, nullptr, &depthViews_[i]);
            result != VK_SUCCESS)
        {
            return std::unexpected(starforge::vulkan::describeFailure("vkCreateImageView (depth)", result));
        }
        {
            const std::string name = "starforge.swapchain.depth." + std::to_string(i);
            setDebugObjectName(device_, reinterpret_cast<std::uint64_t>(depthImages_[i]), VK_OBJECT_TYPE_IMAGE, name.c_str());
            const std::string viewName = "starforge.swapchain.depth_view." + std::to_string(i);
            setDebugObjectName(device_, reinterpret_cast<std::uint64_t>(depthViews_[i]), VK_OBJECT_TYPE_IMAGE_VIEW, viewName.c_str());
        }
    }

    return {};
}

void VulkanSwapchain::destroyDepthImages() noexcept
{
    if (device_ == VK_NULL_HANDLE)
    {
        depthImages_.clear();
        depthViews_.clear();
        depthMemory_ = VK_NULL_HANDLE;
        return;
    }
    for (auto view : depthViews_)
    {
        if (view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device_, view, nullptr);
        }
    }
    for (auto image : depthImages_)
    {
        if (image != VK_NULL_HANDLE)
        {
            vkDestroyImage(device_, image, nullptr);
        }
    }
    if (depthMemory_ != VK_NULL_HANDLE)
    {
        vkFreeMemory(device_, depthMemory_, nullptr);
    }
    depthImages_.clear();
    depthViews_.clear();
    depthMemory_ = VK_NULL_HANDLE;
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
        if (const VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(info.physicalDevice, info.surface, &formatCount, formats.data());
            result != VK_SUCCESS && result != VK_INCOMPLETE)
        {
            return std::unexpected(starforge::vulkan::describeFailure("vkGetPhysicalDeviceSurfaceFormatsKHR", result));
        }
        formats.resize(formatCount);
    }

    const auto surfaceFormat = chooseSurfaceFormat(formats);
    if (!surfaceFormat)
    {
        return std::unexpected(surfaceFormat.error());
    }

    std::uint32_t presentModeCount = 0;
    if (const VkResult result = vkGetPhysicalDeviceSurfacePresentModesKHR(info.physicalDevice, info.surface, &presentModeCount, nullptr);
        result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkGetPhysicalDeviceSurfacePresentModesKHR", result));
    }
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    if (presentModeCount > 0)
    {
        if (const VkResult result = vkGetPhysicalDeviceSurfacePresentModesKHR(info.physicalDevice, info.surface, &presentModeCount, presentModes.data());
            result != VK_SUCCESS && result != VK_INCOMPLETE)
        {
            return std::unexpected(starforge::vulkan::describeFailure("vkGetPhysicalDeviceSurfacePresentModesKHR", result));
        }
        presentModes.resize(presentModeCount);
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
    setDebugObjectName(info.device, reinterpret_cast<std::uint64_t>(swapchain.swapchain_), VK_OBJECT_TYPE_SWAPCHAIN_KHR, "starforge.swapchain");

    if (const VkResult result = vkGetSwapchainImagesKHR(info.device, swapchain.swapchain_, &imageCount, nullptr);
        result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkGetSwapchainImagesKHR", result));
    }
    swapchain.images_.resize(imageCount);
    if (const VkResult result = vkGetSwapchainImagesKHR(info.device, swapchain.swapchain_, &imageCount, swapchain.images_.data());
        result != VK_SUCCESS && result != VK_INCOMPLETE)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkGetSwapchainImagesKHR", result));
    }
    swapchain.images_.resize(imageCount);

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
        {
            const std::string name = "starforge.swapchain.color." + std::to_string(i);
            setDebugObjectName(info.device, reinterpret_cast<std::uint64_t>(swapchain.images_[i]), VK_OBJECT_TYPE_IMAGE, name.c_str());
            const std::string viewName = "starforge.swapchain.color_view." + std::to_string(i);
            setDebugObjectName(info.device, reinterpret_cast<std::uint64_t>(swapchain.imageViews_[i]), VK_OBJECT_TYPE_IMAGE_VIEW, viewName.c_str());
        }
    }

    if (auto status = swapchain.createDepthImages(imageCount); !status)
    {
        return std::unexpected(std::move(status.error()));
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

    *this = std::move(*replacement);
    return {};
}

std::expected<void, std::string> VulkanSwapchain::recreate(std::uint32_t width, std::uint32_t height,
                                                           GpuRetirementQueue& retirement,
                                                           VkFence lastUseFence)
{
    if (width == 0 || height == 0)
    {
        return std::unexpected("Swapchain extent cannot be zero");
    }
    if (device_ == VK_NULL_HANDLE)
    {
        return std::unexpected("Swapchain not initialized");
    }
    if (lastUseFence == VK_NULL_HANDLE)
    {
        return recreate(width, height);
    }

    CreateInfo newInfo = info_;
    newInfo.width = width;
    newInfo.height = height;

    auto replacement = build(newInfo, swapchain_);
    if (!replacement)
    {
        return std::unexpected(std::move(replacement.error()));
    }

    // Retire old resources; new swapchain already created with oldSwapchain handle.
    VkDevice dev = device_;
    VkSwapchainKHR oldSwapchain = std::exchange(swapchain_, VK_NULL_HANDLE);
    auto oldViews = std::move(imageViews_);
    auto oldDepthViews = std::move(depthViews_);
    auto oldDepthImages = std::move(depthImages_);
    VkDeviceMemory oldDepthMemory = std::exchange(depthMemory_, VK_NULL_HANDLE);

    // Adopt replacement without destroying old handles we extracted.
    device_ = std::exchange(replacement->device_, VK_NULL_HANDLE);
    swapchain_ = std::exchange(replacement->swapchain_, VK_NULL_HANDLE);
    format_ = replacement->format_;
    colorSpace_ = replacement->colorSpace_;
    presentMode_ = replacement->presentMode_;
    extent_ = replacement->extent_;
    images_ = std::move(replacement->images_);
    imageViews_ = std::move(replacement->imageViews_);
    depthFormat_ = std::exchange(replacement->depthFormat_, VK_FORMAT_UNDEFINED);
    depthImages_ = std::move(replacement->depthImages_);
    depthViews_ = std::move(replacement->depthViews_);
    depthMemory_ = std::exchange(replacement->depthMemory_, VK_NULL_HANDLE);
    info_ = replacement->info_;
    // replacement now holds only null/empty resources; its destructor is harmless.

    retirement.retire(lastUseFence, [dev, oldSwapchain, oldViews = std::move(oldViews),
                                     oldDepthViews = std::move(oldDepthViews),
                                     oldDepthImages = std::move(oldDepthImages),
                                     oldDepthMemory]() mutable {
        for (VkImageView view : oldDepthViews)
        {
            if (view != VK_NULL_HANDLE) vkDestroyImageView(dev, view, nullptr);
        }
        for (VkImage image : oldDepthImages)
        {
            if (image != VK_NULL_HANDLE) vkDestroyImage(dev, image, nullptr);
        }
        if (oldDepthMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(dev, oldDepthMemory, nullptr);
        }
        for (VkImageView view : oldViews)
        {
            if (view != VK_NULL_HANDLE) vkDestroyImageView(dev, view, nullptr);
        }
        if (oldSwapchain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(dev, oldSwapchain, nullptr);
        }
    });

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
      depthFormat_(std::exchange(other.depthFormat_, VK_FORMAT_UNDEFINED)),
      depthImages_(std::move(other.depthImages_)),
      depthViews_(std::move(other.depthViews_)),
      depthMemory_(std::exchange(other.depthMemory_, VK_NULL_HANDLE)),
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
        depthFormat_ = std::exchange(other.depthFormat_, VK_FORMAT_UNDEFINED);
        depthImages_ = std::move(other.depthImages_);
        depthViews_ = std::move(other.depthViews_);
        depthMemory_ = std::exchange(other.depthMemory_, VK_NULL_HANDLE);
        info_ = other.info_;
    }
    return *this;
}

void VulkanSwapchain::destroy() noexcept
{
    destroyDepthImages();
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
