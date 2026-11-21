#include "vulkan/hdr_target.hpp"

#include "vulkan/vulkan_result.hpp"

#include <utility>

namespace starforge::vulkan
{

namespace
{
[[nodiscard]] std::expected<std::uint32_t, std::string>
findDeviceLocalMemoryType(VkPhysicalDevice physicalDevice, std::uint32_t typeBits)
{
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &props);
    for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i)
    {
        const bool allowed = (typeBits & (1u << i)) != 0;
        const bool deviceLocal = (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
        if (allowed && deviceLocal)
        {
            return i;
        }
    }
    return std::unexpected("HdrTarget: no device-local memory type for scene colour image");
}
} // namespace

std::expected<HdrTarget, std::string> HdrTarget::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE || info.physicalDevice == VK_NULL_HANDLE)
    {
        return std::unexpected("HdrTarget: invalid device");
    }
    if (info.count == 0 || info.extent.width == 0 || info.extent.height == 0)
    {
        return std::unexpected("HdrTarget: empty extent or zero image count");
    }

    // Everything the scene + bloom + tonemap chain needs from this format.
    VkFormatProperties formatProps{};
    vkGetPhysicalDeviceFormatProperties(info.physicalDevice, info.format, &formatProps);
    const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                                          VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
                                          VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((formatProps.optimalTilingFeatures & required) != required)
    {
        return std::unexpected("HdrTarget: requested format lacks attachment/blend/linear-sample support");
    }

    HdrTarget target;
    target.device_ = info.device;
    target.format_ = info.format;
    target.extent_ = info.extent;
    target.images_.assign(info.count, VK_NULL_HANDLE);
    target.memory_.assign(info.count, VK_NULL_HANDLE);
    target.views_.assign(info.count, VK_NULL_HANDLE);

    for (std::uint32_t i = 0; i < info.count; ++i)
    {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = info.format;
        imageInfo.extent = {info.extent.width, info.extent.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | info.extraUsage;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (const VkResult r = vkCreateImage(info.device, &imageInfo, nullptr, &target.images_[i]); r != VK_SUCCESS)
        {
            return std::unexpected(describeFailure("vkCreateImage (hdr)", r));
        }

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(info.device, target.images_[i], &req);
        auto typeIndex = findDeviceLocalMemoryType(info.physicalDevice, req.memoryTypeBits);
        if (!typeIndex)
        {
            return std::unexpected(std::move(typeIndex.error()));
        }

        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = *typeIndex;
        if (const VkResult r = vkAllocateMemory(info.device, &alloc, nullptr, &target.memory_[i]); r != VK_SUCCESS)
        {
            return std::unexpected(describeFailure("vkAllocateMemory (hdr)", r));
        }
        if (const VkResult r = vkBindImageMemory(info.device, target.images_[i], target.memory_[i], 0);
            r != VK_SUCCESS)
        {
            return std::unexpected(describeFailure("vkBindImageMemory (hdr)", r));
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = target.images_[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = info.format;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (const VkResult r = vkCreateImageView(info.device, &viewInfo, nullptr, &target.views_[i]);
            r != VK_SUCCESS)
        {
            return std::unexpected(describeFailure("vkCreateImageView (hdr)", r));
        }
    }
    // Early returns above are safe: ~HdrTarget() frees whatever was created.
    return target;
}

HdrTarget::~HdrTarget()
{
    destroy();
}

HdrTarget::HdrTarget(HdrTarget&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      format_(other.format_),
      extent_(other.extent_),
      images_(std::move(other.images_)),
      memory_(std::move(other.memory_)),
      views_(std::move(other.views_))
{
    other.images_.clear();
    other.memory_.clear();
    other.views_.clear();
}

HdrTarget& HdrTarget::operator=(HdrTarget&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        format_ = other.format_;
        extent_ = other.extent_;
        images_ = std::move(other.images_);
        memory_ = std::move(other.memory_);
        views_ = std::move(other.views_);
        other.images_.clear();
        other.memory_.clear();
        other.views_.clear();
    }
    return *this;
}

void HdrTarget::destroy() noexcept
{
    if (device_ != VK_NULL_HANDLE)
    {
        for (VkImageView v : views_)
        {
            if (v != VK_NULL_HANDLE) vkDestroyImageView(device_, v, nullptr);
        }
        for (VkImage img : images_)
        {
            if (img != VK_NULL_HANDLE) vkDestroyImage(device_, img, nullptr);
        }
        for (VkDeviceMemory m : memory_)
        {
            if (m != VK_NULL_HANDLE) vkFreeMemory(device_, m, nullptr);
        }
    }
    device_ = VK_NULL_HANDLE;
    images_.clear();
    memory_.clear();
    views_.clear();
}

} // namespace starforge::vulkan
