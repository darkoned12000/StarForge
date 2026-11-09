#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <vector>

namespace starforge::vulkan
{

class VulkanSwapchain;

enum class FrameStatus
{
    Presented,
    SwapchainOutOfDate,
};

// Records dynamic-rendering clear commands into `cmd`. Transitions `image`
// from UNDEFINED to COLOR_ATTACHMENT_OPTIMAL, clears with `clearColor`, and
// transitions to `finalLayout` (e.g. VK_IMAGE_LAYOUT_PRESENT_SRC_KHR or
// VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL for headless readback tests).
// When `depthImage`/`depthView` are supplied, also transitions the depth
// image UNDEFINED -> DEPTH_ATTACHMENT_OPTIMAL, clears depth to
// `clearDepth`, and transitions it to `finalLayout`.
void recordClearColorImage(VkCommandBuffer cmd,
                           VkImage image,
                           VkImageView imageView,
                           VkExtent2D extent,
                           VkClearColorValue clearColor,
                           VkImageLayout finalLayout,
                           VkPipelineStageFlags2 dstStageMask,
                           VkAccessFlags2 dstAccessMask,
                           VkImage depthImage = VK_NULL_HANDLE,
                           VkImageView depthView = VK_NULL_HANDLE,
                           float clearDepth = 1.0f,
                           VkFormat depthFormat = VK_FORMAT_UNDEFINED);

class FrameRenderer
{
public:
    struct CreateInfo
    {
        VkDevice device = VK_NULL_HANDLE;
        std::uint32_t queueFamily = 0;
        std::uint32_t framesInFlight = 2;
        std::uint32_t swapchainImageCount = 3;
    };

    [[nodiscard]] static std::expected<FrameRenderer, std::string> create(const CreateInfo& info);

    ~FrameRenderer();
    FrameRenderer(FrameRenderer&& other) noexcept;
    FrameRenderer& operator=(FrameRenderer&& other) noexcept;
    FrameRenderer(const FrameRenderer&) = delete;
    FrameRenderer& operator=(const FrameRenderer&) = delete;

    // Resizes the per-image render-finished semaphore pool if a recreated
    // swapchain has more images than previously allocated.
    [[nodiscard]] std::expected<void, std::string> ensureSwapchainImageCount(std::uint32_t imageCount);

    // Waits on the current frame's fence, acquires an image, records a dynamic
    // rendering clear to `clearColor`, submits via vkQueueSubmit2, and presents.
    [[nodiscard]] std::expected<FrameStatus, std::string>
    renderClearedFrame(const VulkanSwapchain& swapchain, VkQueue queue, VkClearColorValue clearColor);

    // Callback invoked inside a single dynamic-rendering pass (clear load
    // op) for drawing additional content with the swapchain image as color
    // attachment. The callback must set viewport/scissor if needed.
    using FrameRecordFn = std::function<void(VkCommandBuffer cmd, VkExtent2D extent)>;

    // Like renderClearedFrame, but records `record` into the swapchain image
    // in the same clear pass before presenting.
    [[nodiscard]] std::expected<FrameStatus, std::string>
    renderFrame(const VulkanSwapchain& swapchain, VkQueue queue, VkClearColorValue clearColor,
                const FrameRecordFn& record);

    [[nodiscard]] VkCommandBuffer currentCommandBuffer() const;
    [[nodiscard]] VkFence currentFence() const;
    void nextFrame();
    [[nodiscard]] std::uint32_t framesInFlight() const noexcept { return framesInFlight_; }

private:
    FrameRenderer() = default;
    void destroy() noexcept;

    [[nodiscard]] std::expected<FrameStatus, std::string>
    renderFrameImpl(const VulkanSwapchain& swapchain, VkQueue queue, VkClearColorValue clearColor,
                    const FrameRecordFn* record);

    VkDevice device_ = VK_NULL_HANDLE;
    std::uint32_t queueFamily_ = 0;
    std::uint32_t framesInFlight_ = 2;
    std::uint32_t currentFrame_ = 0;
    std::vector<VkCommandPool> commandPools_;
    std::vector<VkCommandBuffer> commandBuffers_;
    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    std::vector<VkFence> inFlightFences_;
    std::vector<VkFence> imagesInFlight_; // sized to swapchain image count
    VkSwapchainKHR lastSwapchain_ = VK_NULL_HANDLE; // detects recreate to reset imagesInFlight_
};

} // namespace starforge::vulkan
