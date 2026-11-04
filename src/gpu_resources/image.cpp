#include "gpu_resources/image.hpp"

#include "vulkan/vulkan_result.hpp"

#include <utility>

namespace starforge::gpu_resources
{

namespace
{

bool hasStencilComponent(VkFormat format)
{
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
           format == VK_FORMAT_D24_UNORM_S8_UINT ||
           format == VK_FORMAT_D16_UNORM_S8_UINT;
}

VkImageUsageFlags toVulkanUsage(ImageUsage usage)
{
    switch (usage)
    {
    case ImageUsage::ColorAttachment:  return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    case ImageUsage::DepthAttachment:   return VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    case ImageUsage::TransferSrc:       return VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    case ImageUsage::TransferDst:       return VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    case ImageUsage::Sampled:          return VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
}

VkImageAspectFlags toAspectMask(VkFormat format)
{
    if (format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D16_UNORM)
    {
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    if (hasStencilComponent(format))
    {
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

} // namespace

std::expected<Image, std::string> Image::create(Allocator& allocator, const CreateInfo& info)
{
    if (info.width == 0 || info.height == 0)
    {
        return std::unexpected("Image dimensions must be > 0");
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = info.format;
    imageInfo.extent = {info.width, info.height, 1};
    imageInfo.mipLevels = info.mipLevels;
    imageInfo.arrayLayers = info.arrayLayers;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = toVulkanUsage(info.usage);
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    Image image;
    image.allocator_ = allocator.handle();
    image.format_ = info.format;
    image.width_ = info.width;
    image.height_ = info.height;
    image.aspectMask_ = toAspectMask(info.format);

    VkResult result = vmaCreateImage(allocator.handle(), &imageInfo, &allocInfo,
                                     &image.image_, &image.allocation_, nullptr);
    if (result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vmaCreateImage", result));
    }

    if (info.name != nullptr)
    {
        vmaSetAllocationName(allocator.handle(), image.allocation_, info.name);
    }

    return image;
}

Image::~Image()
{
    if (allocator_ != nullptr && image_ != VK_NULL_HANDLE)
    {
        vmaDestroyImage(allocator_, image_, allocation_);
    }
    image_ = VK_NULL_HANDLE;
    allocation_ = nullptr;
    allocator_ = nullptr;
}

Image::Image(Image&& other) noexcept
    : allocator_(std::exchange(other.allocator_, nullptr)),
      image_(std::exchange(other.image_, VK_NULL_HANDLE)),
      allocation_(std::exchange(other.allocation_, nullptr)),
      format_(other.format_),
      width_(other.width_),
      height_(other.height_),
      aspectMask_(other.aspectMask_)
{
}

Image& Image::operator=(Image&& other) noexcept
{
    if (this != &other)
    {
        if (allocator_ != nullptr && image_ != VK_NULL_HANDLE)
        {
            vmaDestroyImage(allocator_, image_, allocation_);
        }
        allocator_ = std::exchange(other.allocator_, nullptr);
        image_ = std::exchange(other.image_, VK_NULL_HANDLE);
        allocation_ = std::exchange(other.allocation_, nullptr);
        format_ = other.format_;
        width_ = other.width_;
        height_ = other.height_;
        aspectMask_ = other.aspectMask_;
    }
    return *this;
}

std::expected<VkImageView, std::string> Image::createView(VkDevice device, VkImageAspectFlags aspect) const
{
    if (image_ == VK_NULL_HANDLE)
    {
        return std::unexpected("Image not initialized");
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format_;
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.subresourceRange.aspectMask = (aspect != 0) ? aspect : aspectMask_;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

    VkImageView view = VK_NULL_HANDLE;
    if (const VkResult result = vkCreateImageView(device, &viewInfo, nullptr, &view);
        result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkCreateImageView", result));
    }
    return view;
}

void Image::transitionLayout(VkCommandBuffer cmd, VkImage image,
                             VkImageLayout oldLayout, VkImageLayout newLayout,
                             VkImageAspectFlags aspectMask,
                             VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                             VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
{
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspectMask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
}

void Image::transitionToColorAttachment(VkCommandBuffer cmd, VkImage image)
{
    transitionLayout(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_IMAGE_ASPECT_COLOR_BIT,
                     VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
}

void Image::transitionToDepthAttachment(VkCommandBuffer cmd, VkImage image, VkFormat format)
{
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    VkImageLayout newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    if (hasStencilComponent(format))
    {
        aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
        newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    transitionLayout(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED, newLayout,
                     aspect,
                     VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                     VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                     VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
}

void Image::transitionToTransferSrc(VkCommandBuffer cmd, VkImage image)
{
    transitionLayout(cmd, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_IMAGE_ASPECT_COLOR_BIT,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
}

void Image::transitionToTransferDst(VkCommandBuffer cmd, VkImage image)
{
    transitionLayout(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_ASPECT_COLOR_BIT,
                     VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                     VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
}

void Image::transitionToShaderReadOnly(VkCommandBuffer cmd, VkImage image)
{
    transitionLayout(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_IMAGE_ASPECT_COLOR_BIT,
                     VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
}

VkFormat findDepthFormat(VkPhysicalDevice physicalDevice)
{
    const VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM,
    };

    for (VkFormat format : candidates)
    {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);
        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
        {
            return format;
        }
    }
    return VK_FORMAT_UNDEFINED;
}

bool formatSupportsFeatures(VkPhysicalDevice physicalDevice, VkFormat format,
                           VkImageTiling tiling, VkFormatFeatureFlags features)
{
    VkFormatProperties props{};
    if (tiling == VK_IMAGE_TILING_OPTIMAL)
    {
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);
        return (props.optimalTilingFeatures & features) == features;
    }
    else if (tiling == VK_IMAGE_TILING_LINEAR)
    {
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);
        return (props.linearTilingFeatures & features) == features;
    }
    return false;
}

} // namespace starforge::gpu_resources