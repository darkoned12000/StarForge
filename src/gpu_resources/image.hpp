#pragma once

#include "gpu_resources/allocator.hpp"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace starforge::gpu_resources
{

enum class ImageUsage
{
    ColorAttachment,
    DepthAttachment,
    TransferSrc,
    TransferDst,
    Sampled,
};

enum class ImageLayout
{
    Undefined,
    ColorAttachmentOptimal,
    DepthAttachmentOptimal,
    TransferSrcOptimal,
    TransferDstOptimal,
    ShaderReadOnlyOptimal,
    PresentSrc,
};

// RAII wrapper for VkImage + VmaAllocation. Move-only.
class Image
{
public:
    struct CreateInfo
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
        ImageUsage usage = ImageUsage::ColorAttachment;
        std::uint32_t mipLevels = 1;
        std::uint32_t arrayLayers = 1;
    };

    Image() = default;
    ~Image();
    Image(Image&& other) noexcept;
    Image& operator=(Image&& other) noexcept;
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    [[nodiscard]] static std::expected<Image, std::string>
    create(Allocator& allocator, const CreateInfo& info);

    [[nodiscard]] VkImage handle() const noexcept { return image_; }
    [[nodiscard]] VmaAllocation allocation() const noexcept { return allocation_; }
    [[nodiscard]] VkFormat format() const noexcept { return format_; }
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] VkImageAspectFlags aspectMask() const noexcept { return aspectMask_; }

    // Create a VkImageView for this image.
    [[nodiscard]] std::expected<VkImageView, std::string>
    createView(VkDevice device, VkImageAspectFlags aspect = 0) const;

    // Transition the image to a new layout via synchronization2.
    static void transitionLayout(VkCommandBuffer cmd, VkImage image,
                                 VkImageLayout oldLayout, VkImageLayout newLayout,
                                 VkImageAspectFlags aspectMask,
                                 VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                 VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);

    // Convenience: transition to a layout using sensible defaults.
    static void transitionToColorAttachment(VkCommandBuffer cmd, VkImage image);
    static void transitionToDepthAttachment(VkCommandBuffer cmd, VkImage image, VkFormat format = VK_FORMAT_D32_SFLOAT);
    static void transitionToTransferSrc(VkCommandBuffer cmd, VkImage image);
    static void transitionToTransferDst(VkCommandBuffer cmd, VkImage image);
    static void transitionToShaderReadOnly(VkCommandBuffer cmd, VkImage image);

private:
    Allocator* allocator_ = nullptr;
    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_R8G8B8A8_UNORM;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    VkImageAspectFlags aspectMask_ = VK_IMAGE_ASPECT_COLOR_BIT;
};

// Helper to find a supported depth format.
[[nodiscard]] VkFormat findDepthFormat(VkPhysicalDevice physicalDevice);

// Check if a format supports a given tiling and feature flags.
[[nodiscard]] bool formatSupportsFeatures(VkPhysicalDevice physicalDevice, VkFormat format,
                                         VkImageTiling tiling, VkFormatFeatureFlags features);

} // namespace starforge::gpu_resources