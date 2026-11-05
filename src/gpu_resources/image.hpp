#pragma once

#include "gpu_resources/allocator.hpp"

#include <vulkan/vulkan.h>
// VMA's intended configuration: disable Clang-specific nullability attributes.
#define VMA_NULLABLE
#define VMA_NOT_NULL
#define VMA_NULLABLE_NON_DISPATCHABLE VMA_NULLABLE
#define VMA_NOT_NULL_NON_DISPATCHABLE VMA_NOT_NULL
#include <vk_mem_alloc.h>

#include <cstdint>
#include <expected>
#include <string>

namespace starforge::gpu_resources
{

// Image usage flags (bitmask). Can be combined with |.
enum class ImageUsageFlags : VkImageUsageFlags
{
    None             = 0,
    ColorAttachment  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    DepthAttachment  = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
    TransferSrc      = VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
    TransferDst      = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    Sampled          = VK_IMAGE_USAGE_SAMPLED_BIT,
};

inline ImageUsageFlags operator|(ImageUsageFlags a, ImageUsageFlags b)
{
    return static_cast<ImageUsageFlags>(static_cast<VkImageUsageFlags>(a) | static_cast<VkImageUsageFlags>(b));
}

inline ImageUsageFlags operator&(ImageUsageFlags a, ImageUsageFlags b)
{
    return static_cast<ImageUsageFlags>(static_cast<VkImageUsageFlags>(a) & static_cast<VkImageUsageFlags>(b));
}

inline bool hasFlag(ImageUsageFlags flags, ImageUsageFlags flag)
{
    return (static_cast<VkImageUsageFlags>(flags) & static_cast<VkImageUsageFlags>(flag)) != 0;
}

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
        ImageUsageFlags usage = ImageUsageFlags::ColorAttachment;
        std::uint32_t mipLevels = 1;
        std::uint32_t arrayLayers = 1;
        const char* name = nullptr;
    };

    // Current layout tracking.
    [[nodiscard]] VkImageLayout currentLayout() const noexcept { return layout_; }

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

    // RAII wrapper for VkImageView.
    class ImageView
    {
    public:
        ImageView() = default;
        ~ImageView();
        ImageView(ImageView&& other) noexcept;
        ImageView& operator=(ImageView&& other) noexcept;
        ImageView(const ImageView&) = delete;
        ImageView& operator=(const ImageView&) = delete;

        [[nodiscard]] VkImageView handle() const noexcept { return view_; }
        [[nodiscard]] bool valid() const noexcept { return view_ != VK_NULL_HANDLE; }

        // Implicit conversion for convenience.
        operator VkImageView() const noexcept { return view_; }

    private:
        friend class Image;
        ImageView(VkDevice device, VkImageView view) : device_(device), view_(view) {}
        VkDevice device_ = VK_NULL_HANDLE;
        VkImageView view_ = VK_NULL_HANDLE;
    };

    // Create an RAII ImageView for this image.
    [[nodiscard]] std::expected<ImageView, std::string>
    createImageView(VkDevice device, VkImageAspectFlags aspect = 0) const;

    // Transition the image to a new layout via synchronization2.
    static void transitionLayout(VkCommandBuffer cmd, VkImage image,
                                 VkImageLayout oldLayout, VkImageLayout newLayout,
                                 VkImageAspectFlags aspectMask,
                                 VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                 VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);

    // Convenience: transition to a layout using sensible defaults.
    static void transitionToColorAttachment(VkCommandBuffer cmd, VkImage image);
    static void transitionToDepthAttachment(VkCommandBuffer cmd, VkImage image, VkFormat format = VK_FORMAT_D32_SFLOAT);

    // Non-static convenience: uses this image's format automatically.
    // Updates internal layout tracking.
    void transitionToDepthAttachment(VkCommandBuffer cmd)
    {
        VkImageLayout newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        if (format_ == VK_FORMAT_D32_SFLOAT_S8_UINT ||
            format_ == VK_FORMAT_D24_UNORM_S8_UINT ||
            format_ == VK_FORMAT_D16_UNORM_S8_UINT)
        {
            newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }
        transitionToDepthAttachment(cmd, image_, format_);
        layout_ = newLayout;
    }

    // Non-static convenience for color attachment.
    void transitionToColorAttachment(VkCommandBuffer cmd)
    {
        transitionToColorAttachment(cmd, image_);
        layout_ = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    // Non-static convenience for transfer src.
    void transitionToTransferSrc(VkCommandBuffer cmd)
    {
        transitionToTransferSrc(cmd, image_);
        layout_ = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }

    // Non-static convenience for transfer dst.
    void transitionToTransferDst(VkCommandBuffer cmd)
    {
        transitionToTransferDst(cmd, image_);
        layout_ = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    }

    // Non-static convenience for shader read only.
    void transitionToShaderReadOnly(VkCommandBuffer cmd)
    {
        transitionToShaderReadOnly(cmd, image_);
        layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    static void transitionToTransferSrc(VkCommandBuffer cmd, VkImage image);
    static void transitionToTransferDst(VkCommandBuffer cmd, VkImage image);
    static void transitionToShaderReadOnly(VkCommandBuffer cmd, VkImage image);

private:
    VmaAllocator allocator_ = nullptr;
    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = nullptr;
    VkFormat format_ = VK_FORMAT_R8G8B8A8_UNORM;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    VkImageAspectFlags aspectMask_ = VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageLayout layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
};

// Helper to find a supported depth format.
[[nodiscard]] VkFormat findDepthFormat(VkPhysicalDevice physicalDevice);

// Check if a format supports a given tiling and feature flags.
[[nodiscard]] bool formatSupportsFeatures(VkPhysicalDevice physicalDevice, VkFormat format,
                                         VkImageTiling tiling, VkFormatFeatureFlags features);

} // namespace starforge::gpu_resources