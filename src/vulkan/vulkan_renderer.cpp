#include "vulkan/vulkan_renderer.hpp"

#include "core/log.hpp"
#include "vulkan/queue_sync.hpp"
#include "vulkan/vulkan_result.hpp"
#include "vulkan/vulkan_swapchain.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace starforge::vulkan
{

namespace
{
constexpr const char* kTag = "VULKAN";

// Depth-only formats must not carry the stencil aspect in barriers.
[[nodiscard]] VkImageAspectFlags depthAspectMask(VkFormat depthFormat) noexcept
{
    return (depthFormat == VK_FORMAT_D24_UNORM_S8_UINT || depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT)
               ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
               : VK_IMAGE_ASPECT_DEPTH_BIT;
}
} // namespace

void recordClearColorImage(VkCommandBuffer cmd,
                           VkImage image,
                           VkImageView imageView,
                           VkExtent2D extent,
                           VkClearColorValue clearColor,
                           VkImageLayout finalLayout,
                           VkPipelineStageFlags2 dstStageMask,
                           VkAccessFlags2 dstAccessMask,
                           VkImage depthImage,
                           VkImageView depthView,
                           float clearDepth,
                           VkFormat depthFormat)
{
    VkImageMemoryBarrier2 toAttachment{};
    toAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toAttachment.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toAttachment.srcAccessMask = 0;
    toAttachment.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toAttachment.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toAttachment.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toAttachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toAttachment.image = image;
    toAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toAttachment.subresourceRange.baseMipLevel = 0;
    toAttachment.subresourceRange.levelCount = 1;
    toAttachment.subresourceRange.baseArrayLayer = 0;
    toAttachment.subresourceRange.layerCount = 1;

    VkDependencyInfo preDep{};
    preDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    preDep.imageMemoryBarrierCount = 1;
    preDep.pImageMemoryBarriers = &toAttachment;
    vkCmdPipelineBarrier2(cmd, &preDep);

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = imageView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = clearColor;

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = depthView;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = VkClearDepthStencilValue{clearDepth, 0};

    VkImageMemoryBarrier2 depthToAttachment{};
    const bool hasDepth = depthImage != VK_NULL_HANDLE && depthView != VK_NULL_HANDLE;
    if (hasDepth)
    {
        depthToAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        depthToAttachment.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
        depthToAttachment.srcAccessMask = 0;
        depthToAttachment.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
        depthToAttachment.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depthToAttachment.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthToAttachment.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthToAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthToAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthToAttachment.image = depthImage;
        depthToAttachment.subresourceRange.aspectMask = depthAspectMask(depthFormat);
        depthToAttachment.subresourceRange.baseMipLevel = 0;
        depthToAttachment.subresourceRange.levelCount = 1;
        depthToAttachment.subresourceRange.baseArrayLayer = 0;
        depthToAttachment.subresourceRange.layerCount = 1;

        VkDependencyInfo depthDep{};
        depthDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depthDep.imageMemoryBarrierCount = 1;
        depthDep.pImageMemoryBarriers = &depthToAttachment;
        vkCmdPipelineBarrier2(cmd, &depthDep);
    }

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = extent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = hasDepth ? &depthAttachment : nullptr;

    vkCmdBeginRendering(cmd, &renderingInfo);
    vkCmdEndRendering(cmd);

    VkImageMemoryBarrier2 toFinal{};
    toFinal.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toFinal.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toFinal.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toFinal.dstStageMask = dstStageMask;
    toFinal.dstAccessMask = dstAccessMask;
    toFinal.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toFinal.newLayout = finalLayout;
    toFinal.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toFinal.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toFinal.image = image;
    toFinal.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toFinal.subresourceRange.baseMipLevel = 0;
    toFinal.subresourceRange.levelCount = 1;
    toFinal.subresourceRange.baseArrayLayer = 0;
    toFinal.subresourceRange.layerCount = 1;

    VkDependencyInfo postDep{};
    postDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    postDep.imageMemoryBarrierCount = 1;
    postDep.pImageMemoryBarriers = &toFinal;
    vkCmdPipelineBarrier2(cmd, &postDep);

    if (hasDepth)
    {
        // The depth image is an offscreen attachment only; it must not be
        // transitioned to PRESENT_SRC (VUID-VkImageMemoryBarrier2-newLayout-01198).
        // It stays in DEPTH_ATTACHMENT_OPTIMAL; this barrier only orders
        // depth writes against later depth consumers. The next frame's
        // UNDEFINED transition discards the contents as usual.
        VkImageMemoryBarrier2 depthToFinal{};
        depthToFinal.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        depthToFinal.srcStageMask =
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        depthToFinal.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depthToFinal.dstStageMask =
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        depthToFinal.dstAccessMask =
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depthToFinal.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthToFinal.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthToFinal.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthToFinal.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthToFinal.image = depthImage;
        depthToFinal.subresourceRange.aspectMask = depthAspectMask(depthFormat);
        depthToFinal.subresourceRange.baseMipLevel = 0;
        depthToFinal.subresourceRange.levelCount = 1;
        depthToFinal.subresourceRange.baseArrayLayer = 0;
        depthToFinal.subresourceRange.layerCount = 1;

        VkDependencyInfo depthPostDep{};
        depthPostDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depthPostDep.imageMemoryBarrierCount = 1;
        depthPostDep.pImageMemoryBarriers = &depthToFinal;
        vkCmdPipelineBarrier2(cmd, &depthPostDep);
    }
}

std::expected<FrameRenderer, std::string> FrameRenderer::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("Invalid device for FrameRenderer");
    }

    FrameRenderer renderer;
    renderer.device_ = info.device;
    renderer.queueFamily_ = info.queueFamily;
    renderer.framesInFlight_ = std::max(1u, info.framesInFlight);

    renderer.commandPools_.resize(renderer.framesInFlight_);
    renderer.commandBuffers_.resize(renderer.framesInFlight_);
    renderer.imageAvailableSemaphores_.resize(renderer.framesInFlight_);
    renderer.inFlightFences_.resize(renderer.framesInFlight_);

    for (std::uint32_t i = 0; i < renderer.framesInFlight_; ++i)
    {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = renderer.queueFamily_;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        if (const VkResult result = vkCreateCommandPool(renderer.device_, &poolInfo, nullptr, &renderer.commandPools_[i]);
            result != VK_SUCCESS)
        {
            return std::unexpected(starforge::vulkan::describeFailure("vkCreateCommandPool", result));
        }

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = renderer.commandPools_[i];
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        if (const VkResult result = vkAllocateCommandBuffers(renderer.device_, &allocInfo, &renderer.commandBuffers_[i]);
            result != VK_SUCCESS)
        {
            return std::unexpected(starforge::vulkan::describeFailure("vkAllocateCommandBuffers", result));
        }

        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (const VkResult result = vkCreateSemaphore(renderer.device_, &semInfo, nullptr, &renderer.imageAvailableSemaphores_[i]);
            result != VK_SUCCESS)
        {
            return std::unexpected(starforge::vulkan::describeFailure("vkCreateSemaphore", result));
        }

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (const VkResult result = vkCreateFence(renderer.device_, &fenceInfo, nullptr, &renderer.inFlightFences_[i]);
            result != VK_SUCCESS)
        {
            return std::unexpected(starforge::vulkan::describeFailure("vkCreateFence", result));
        }
    }

    if (auto status = renderer.ensureSwapchainImageCount(std::max(1u, info.swapchainImageCount)); !status)
    {
        return std::unexpected(std::move(status.error()));
    }

    core::logInfo(kTag, "Frame renderer created with {} frames in flight", renderer.framesInFlight_);
    return renderer;
}

std::expected<void, std::string> FrameRenderer::ensureSwapchainImageCount(std::uint32_t imageCount)
{
    if (device_ == VK_NULL_HANDLE)
    {
        return std::unexpected("FrameRenderer not initialized");
    }

    while (renderFinishedSemaphores_.size() < imageCount)
    {
        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkSemaphore sem = VK_NULL_HANDLE;
        if (const VkResult result = vkCreateSemaphore(device_, &semInfo, nullptr, &sem);
            result != VK_SUCCESS)
        {
            return std::unexpected(starforge::vulkan::describeFailure("vkCreateSemaphore", result));
        }
        renderFinishedSemaphores_.push_back(sem);
    }
    if (imagesInFlight_.size() < imageCount)
    {
        imagesInFlight_.resize(imageCount, VK_NULL_HANDLE);
    }
    return {};
}

FrameRenderer::~FrameRenderer()
{
    destroy();
}

FrameRenderer::FrameRenderer(FrameRenderer&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      queueFamily_(other.queueFamily_),
      framesInFlight_(other.framesInFlight_),
      currentFrame_(other.currentFrame_),
      commandPools_(std::move(other.commandPools_)),
      commandBuffers_(std::move(other.commandBuffers_)),
      imageAvailableSemaphores_(std::move(other.imageAvailableSemaphores_)),
      renderFinishedSemaphores_(std::move(other.renderFinishedSemaphores_)),
      inFlightFences_(std::move(other.inFlightFences_)),
      imagesInFlight_(std::move(other.imagesInFlight_)),
      lastSwapchain_(std::exchange(other.lastSwapchain_, VK_NULL_HANDLE))
{
}

FrameRenderer& FrameRenderer::operator=(FrameRenderer&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        queueFamily_ = other.queueFamily_;
        framesInFlight_ = other.framesInFlight_;
        currentFrame_ = other.currentFrame_;
        commandPools_ = std::move(other.commandPools_);
        commandBuffers_ = std::move(other.commandBuffers_);
        imageAvailableSemaphores_ = std::move(other.imageAvailableSemaphores_);
        renderFinishedSemaphores_ = std::move(other.renderFinishedSemaphores_);
        inFlightFences_ = std::move(other.inFlightFences_);
        imagesInFlight_ = std::move(other.imagesInFlight_);
        lastSwapchain_ = std::exchange(other.lastSwapchain_, VK_NULL_HANDLE);
    }
    return *this;
}

void FrameRenderer::destroy() noexcept
{
    if (device_ != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(device_);
        for (VkSemaphore sem : renderFinishedSemaphores_)
        {
            if (sem != VK_NULL_HANDLE) vkDestroySemaphore(device_, sem, nullptr);
        }
        for (std::uint32_t i = 0; i < commandPools_.size(); ++i)
        {
            if (inFlightFences_[i] != VK_NULL_HANDLE) vkDestroyFence(device_, inFlightFences_[i], nullptr);
            if (imageAvailableSemaphores_[i] != VK_NULL_HANDLE) vkDestroySemaphore(device_, imageAvailableSemaphores_[i], nullptr);
            if (commandPools_[i] != VK_NULL_HANDLE) vkDestroyCommandPool(device_, commandPools_[i], nullptr);
        }
    }
    device_ = VK_NULL_HANDLE;
    commandPools_.clear();
    commandBuffers_.clear();
    imageAvailableSemaphores_.clear();
    renderFinishedSemaphores_.clear();
    inFlightFences_.clear();
    imagesInFlight_.clear();
    lastSwapchain_ = VK_NULL_HANDLE;
}

VkCommandBuffer FrameRenderer::currentCommandBuffer() const
{
    if (currentFrame_ < commandBuffers_.size())
    {
        return commandBuffers_.at(currentFrame_);
    }
    return VK_NULL_HANDLE;
}

VkFence FrameRenderer::currentFence() const
{
    if (currentFrame_ < inFlightFences_.size())
    {
        return inFlightFences_.at(currentFrame_);
    }
    return VK_NULL_HANDLE;
}

void FrameRenderer::nextFrame()
{
    currentFrame_ = (currentFrame_ + 1) % framesInFlight_;
}

std::expected<FrameStatus, std::string>
FrameRenderer::renderFrameImpl(const VulkanSwapchain& swapchain, VkQueue queue, VkClearColorValue clearColor,
                               const FrameRecordFn* record, const OffscreenSceneTarget* target)
{
    if (device_ == VK_NULL_HANDLE || currentFrame_ >= inFlightFences_.size())
    {
        return std::unexpected("FrameRenderer not initialized");
    }

    const auto& images = swapchain.images();
    const auto& views = swapchain.imageViews();
    if (images.empty() || views.size() != images.size())
    {
        return std::unexpected("Swapchain has no valid images");
    }

    if (auto status = ensureSwapchainImageCount(static_cast<std::uint32_t>(images.size())); !status)
    {
        return std::unexpected(std::move(status.error()));
    }

    // A recreated swapchain yields a new VkSwapchainKHR handle; per-image
    // in-flight tracking from the old swapchain is stale and must reset.
    if (swapchain.swapchain() != lastSwapchain_)
    {
        lastSwapchain_ = swapchain.swapchain();
        std::fill(imagesInFlight_.begin(), imagesInFlight_.end(), VK_NULL_HANDLE);
    }

    VkFence fence = inFlightFences_.at(currentFrame_);
    if (const VkResult result = vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX); result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkWaitForFences", result));
    }

    VkSemaphore imageAvailable = imageAvailableSemaphores_.at(currentFrame_);
    std::uint32_t imageIndex = 0;
    const VkResult acquireResult = vkAcquireNextImageKHR(
        device_, swapchain.swapchain(), UINT64_MAX, imageAvailable, VK_NULL_HANDLE, &imageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        // Advance the frame so the next acquire does not reuse a semaphore
        // the driver may have signaled (VUID-vkAcquireNextImageKHR-semaphore-01286).
        nextFrame();
        return FrameStatus::SwapchainOutOfDate;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkAcquireNextImageKHR", acquireResult));
    }

    // If this swapchain image is still in use by a previous frame, wait on
    // that frame's fence before reusing the image.
    if (imageIndex < imagesInFlight_.size() && imagesInFlight_[imageIndex] != VK_NULL_HANDLE)
    {
        if (const VkResult result = vkWaitForFences(device_, 1, &imagesInFlight_[imageIndex], VK_TRUE, UINT64_MAX);
            result != VK_SUCCESS)
        {
            return std::unexpected(starforge::vulkan::describeFailure("vkWaitForFences (image in flight)", result));
        }
    }
    if (imageIndex < imagesInFlight_.size())
    {
        imagesInFlight_[imageIndex] = fence;
    }

    if (const VkResult result = vkResetFences(device_, 1, &fence); result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkResetFences", result));
    }

    VkCommandBuffer cmd = commandBuffers_.at(currentFrame_);
    if (const VkResult result = vkResetCommandBuffer(cmd, 0); result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkResetCommandBuffer", result));
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (const VkResult result = vkBeginCommandBuffer(cmd, &beginInfo); result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkBeginCommandBuffer", result));
    }

    const bool hasDepth = imageIndex < swapchain.depthViews().size();
    const VkImage depthImage = hasDepth ? swapchain.depthImages().at(imageIndex) : VK_NULL_HANDLE;
    const VkImageView depthView = hasDepth ? swapchain.depthViews().at(imageIndex) : VK_NULL_HANDLE;

    if (record == nullptr)
    {
        recordClearColorImage(
            cmd,
            images.at(imageIndex),
            views.at(imageIndex),
            swapchain.extent(),
            clearColor,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            0,
            depthImage,
            depthView,
            1.0f,
            swapchain.depthFormat());
    }
    else
    {
        const bool offscreen = target != nullptr &&
                                 target->colorImages != nullptr &&
                                 target->colorViews != nullptr &&
                                 target->frameCount > currentFrame_;
        const std::uint32_t frameSlot = currentFrame_;
        const VkImage sceneColor = offscreen ? target->colorImages[frameSlot] : images.at(imageIndex);
        const VkImageView sceneView = offscreen ? target->colorViews[frameSlot] : views.at(imageIndex);
        const VkImage sceneDepth = (offscreen && target->depthImages != nullptr && target->depthViews != nullptr)
                                       ? target->depthImages[frameSlot]
                                       : depthImage;
        const VkImageView sceneDepthView = (offscreen && target->depthImages != nullptr && target->depthViews != nullptr)
                                               ? target->depthViews[frameSlot]
                                               : depthView;
        const VkFormat sceneDepthFormat = offscreen ? target->depthFormat : swapchain.depthFormat();
        const VkExtent2D sceneExtent = offscreen ? target->extent : swapchain.extent();
        const bool sceneHasDepth = sceneDepth != VK_NULL_HANDLE && sceneDepthView != VK_NULL_HANDLE;

        // Single pass: transition to attachment, clear, and draw in the
        // same rendering instance (avoids a store+load round-trip that a
        // separate clear pass would cause on tile-based GPUs).
        VkImageMemoryBarrier2 toAttachment{};
        toAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        toAttachment.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        toAttachment.srcAccessMask = 0;
        toAttachment.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        toAttachment.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        toAttachment.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toAttachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toAttachment.image = sceneColor;
        toAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toAttachment.subresourceRange.baseMipLevel = 0;
        toAttachment.subresourceRange.levelCount = 1;
        toAttachment.subresourceRange.baseArrayLayer = 0;
        toAttachment.subresourceRange.layerCount = 1;

        VkDependencyInfo preDep{};
        preDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        preDep.imageMemoryBarrierCount = 1;
        preDep.pImageMemoryBarriers = &toAttachment;
        vkCmdPipelineBarrier2(cmd, &preDep);

        if (sceneHasDepth)
        {
            VkImageMemoryBarrier2 depthPre{};
            depthPre.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            depthPre.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
            depthPre.srcAccessMask = 0;
            depthPre.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
            depthPre.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            depthPre.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depthPre.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depthPre.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            depthPre.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            depthPre.image = sceneDepth;
            depthPre.subresourceRange.aspectMask = depthAspectMask(sceneDepthFormat);
            depthPre.subresourceRange.baseMipLevel = 0;
            depthPre.subresourceRange.levelCount = 1;
            depthPre.subresourceRange.baseArrayLayer = 0;
            depthPre.subresourceRange.layerCount = 1;

            VkDependencyInfo depthDep{};
            depthDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            depthDep.imageMemoryBarrierCount = 1;
            depthDep.pImageMemoryBarriers = &depthPre;
            vkCmdPipelineBarrier2(cmd, &depthDep);
        }

        VkRenderingAttachmentInfo attachment{};
        attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        attachment.imageView = sceneView;
        attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.clearValue.color = clearColor;

        VkRenderingAttachmentInfo depthAttachment{};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = sceneDepthView;
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.clearValue.depthStencil = VkClearDepthStencilValue{1.0f, 0};

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea.offset = {0, 0};
        renderingInfo.renderArea.extent = sceneExtent;
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &attachment;
        renderingInfo.pDepthAttachment = sceneHasDepth ? &depthAttachment : nullptr;

        vkCmdBeginRendering(cmd, &renderingInfo);
        (*record)(cmd, sceneExtent);
        vkCmdEndRendering(cmd);

        if (offscreen)
        {
            // Scene colour -> shader-readable for the tonemap pass. Return
            // both offscreen images to UNDEFINED afterwards: the next frame
            // clears them, so no contents need preserving and the barriers
            // above can always use UNDEFINED as their old layout.
            VkImageMemoryBarrier2 sceneRead{};
            sceneRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            sceneRead.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            sceneRead.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            sceneRead.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            sceneRead.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            sceneRead.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            sceneRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            sceneRead.image = sceneColor;
            sceneRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            sceneRead.subresourceRange.levelCount = 1;
            sceneRead.subresourceRange.layerCount = 1;
            VkDependencyInfo sceneReadDep{};
            sceneReadDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            sceneReadDep.imageMemoryBarrierCount = 1;
            sceneReadDep.pImageMemoryBarriers = &sceneRead;
            vkCmdPipelineBarrier2(cmd, &sceneReadDep);

            // Order pass-1 depth writes against pass 2's LOAD of that depth.
            if (sceneHasDepth)
            {
                VkImageMemoryBarrier2 depthMid{};
                depthMid.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                depthMid.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                depthMid.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                depthMid.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                depthMid.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                depthMid.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
                depthMid.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
                depthMid.image = sceneDepth;
                depthMid.subresourceRange.aspectMask = depthAspectMask(sceneDepthFormat);
                depthMid.subresourceRange.levelCount = 1;
                depthMid.subresourceRange.layerCount = 1;
                VkDependencyInfo depthMidDep{};
                depthMidDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                depthMidDep.imageMemoryBarrierCount = 1;
                depthMidDep.pImageMemoryBarriers = &depthMid;
                vkCmdPipelineBarrier2(cmd, &depthMidDep);
            }

            // Optional bloom chain runs outside any rendering instance.
            if (target->bloom)
                target->bloom(cmd, sceneExtent, sceneView, frameSlot);

            VkImageMemoryBarrier2 barriers[2]{};
            barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barriers[0].srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            barriers[0].srcAccessMask = 0;
            barriers[0].dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            barriers[0].dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barriers[0].image = images.at(imageIndex);
            barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barriers[0].subresourceRange.levelCount = 1;
            barriers[0].subresourceRange.layerCount = 1;

            std::uint32_t barrierCount = 1;
            // Keep swapchain depth in a defined layout to avoid validation noise
            // in HDR/offscreen mode, where pass 2 uses the offscreen depth.
            if (depthImage != VK_NULL_HANDLE && depthImage != sceneDepth)
            {
                barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                barriers[1].srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                           VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                barriers[1].srcAccessMask = 0;
                barriers[1].dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                           VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                barriers[1].dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                barriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
                barriers[1].image = depthImage;
                barriers[1].subresourceRange.aspectMask = depthAspectMask(swapchain.depthFormat());
                barriers[1].subresourceRange.levelCount = 1;
                barriers[1].subresourceRange.layerCount = 1;
                barrierCount = 2;
            }

            VkDependencyInfo swapDep{};
            swapDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            swapDep.imageMemoryBarrierCount = barrierCount;
            swapDep.pImageMemoryBarriers = barriers;
            vkCmdPipelineBarrier2(cmd, &swapDep);

            VkRenderingAttachmentInfo presentAttachment{};
            presentAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            presentAttachment.imageView = views.at(imageIndex);
            presentAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            presentAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // resolve covers every pixel
            presentAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            // Keep the swapchain depth attached with LOAD: the UI pipelines
            // were created with a depth attachment format, and dynamic
            // rendering requires the pipeline format to match the pass.
            VkRenderingAttachmentInfo presentDepth{};
            presentDepth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            // Use the SAME depth as the scene pass: the swapchain depth was
            // never transitioned when rendering offscreen.
            presentDepth.imageView = sceneDepthView;
            presentDepth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            presentDepth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            presentDepth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

            VkRenderingInfo presentRendering{};
            presentRendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            presentRendering.renderArea.offset = {0, 0};
            presentRendering.renderArea.extent = swapchain.extent();
            presentRendering.layerCount = 1;
            presentRendering.colorAttachmentCount = 1;
            presentRendering.pColorAttachments = &presentAttachment;
            presentRendering.pDepthAttachment = sceneHasDepth ? &presentDepth : nullptr;
            vkCmdBeginRendering(cmd, &presentRendering);
            if (target->resolve)
                target->resolve(cmd, swapchain.extent(), sceneView, frameSlot);
            vkCmdEndRendering(cmd);

            // Leave the HDR target in an attachment layout for the next frame.
            VkImageMemoryBarrier2 sceneReset{};
            sceneReset.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            sceneReset.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            sceneReset.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            sceneReset.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
            sceneReset.dstAccessMask = 0;
            sceneReset.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            sceneReset.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            sceneReset.image = sceneColor;
            sceneReset.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            sceneReset.subresourceRange.levelCount = 1;
            sceneReset.subresourceRange.layerCount = 1;
            VkDependencyInfo sceneResetDep{};
            sceneResetDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            sceneResetDep.imageMemoryBarrierCount = 1;
            sceneResetDep.pImageMemoryBarriers = &sceneReset;
            vkCmdPipelineBarrier2(cmd, &sceneResetDep);

            if (sceneHasDepth)
            {
                VkImageMemoryBarrier2 depthReset{};
                depthReset.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                depthReset.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                          VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                depthReset.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                depthReset.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
                depthReset.dstAccessMask = 0;
                depthReset.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
                depthReset.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
                depthReset.image = sceneDepth;
                depthReset.subresourceRange.aspectMask = depthAspectMask(sceneDepthFormat);
                depthReset.subresourceRange.levelCount = 1;
                depthReset.subresourceRange.layerCount = 1;
                VkDependencyInfo depthResetDep{};
                depthResetDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                depthResetDep.imageMemoryBarrierCount = 1;
                depthResetDep.pImageMemoryBarriers = &depthReset;
                vkCmdPipelineBarrier2(cmd, &depthResetDep);
            }
        }
        else if (hasDepth)
        {
            VkImageMemoryBarrier2 depthPost{};
            depthPost.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            // Depth is an offscreen attachment: keep it in
            // DEPTH_ATTACHMENT_OPTIMAL (never PRESENT_SRC) and only order
            // depth writes against later depth consumers.
            depthPost.srcStageMask =
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            depthPost.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            depthPost.dstStageMask =
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            depthPost.dstAccessMask =
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            depthPost.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depthPost.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            depthPost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            depthPost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            depthPost.image = depthImage;
            depthPost.subresourceRange.aspectMask = depthAspectMask(swapchain.depthFormat());
            depthPost.subresourceRange.baseMipLevel = 0;
            depthPost.subresourceRange.levelCount = 1;
            depthPost.subresourceRange.baseArrayLayer = 0;
            depthPost.subresourceRange.layerCount = 1;

            VkDependencyInfo depthPostDep{};
            depthPostDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            depthPostDep.imageMemoryBarrierCount = 1;
            depthPostDep.pImageMemoryBarriers = &depthPost;
            vkCmdPipelineBarrier2(cmd, &depthPostDep);
        }

        VkImageMemoryBarrier2 toPresent{};
        toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        toPresent.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        toPresent.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        toPresent.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        toPresent.dstAccessMask = 0;
        toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.image = images.at(imageIndex);
        toPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toPresent.subresourceRange.baseMipLevel = 0;
        toPresent.subresourceRange.levelCount = 1;
        toPresent.subresourceRange.baseArrayLayer = 0;
        toPresent.subresourceRange.layerCount = 1;

        VkDependencyInfo postDep{};
        postDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        postDep.imageMemoryBarrierCount = 1;
        postDep.pImageMemoryBarriers = &toPresent;
        vkCmdPipelineBarrier2(cmd, &postDep);
    }

    if (const VkResult result = vkEndCommandBuffer(cmd); result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkEndCommandBuffer", result));
    }

    VkSemaphore renderFinished = renderFinishedSemaphores_.at(imageIndex);

    VkSemaphoreSubmitInfo waitSemInfo{};
    waitSemInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitSemInfo.semaphore = imageAvailable;
    waitSemInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkCommandBufferSubmitInfo cmdSubmitInfo{};
    cmdSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmitInfo.commandBuffer = cmd;

    VkSemaphoreSubmitInfo signalSemInfo{};
    signalSemInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalSemInfo.semaphore = renderFinished;
    signalSemInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.waitSemaphoreInfoCount = 1;
    submitInfo.pWaitSemaphoreInfos = &waitSemInfo;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdSubmitInfo;
    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalSemInfo;

    if (const VkResult result = submit2(queue, 1, &submitInfo, fence); result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkQueueSubmit2", result));
    }

    VkSwapchainKHR swapchainHandle = swapchain.swapchain();
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchainHandle;
    presentInfo.pImageIndices = &imageIndex;

    const VkResult presentResult = vkQueuePresentKHR(queue, &presentInfo);
    nextFrame();

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || acquireResult == VK_SUBOPTIMAL_KHR)
    {
        return FrameStatus::SwapchainOutOfDate;
    }
    if (presentResult != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkQueuePresentKHR", presentResult));
    }

    return FrameStatus::Presented;
}

std::expected<FrameStatus, std::string>
FrameRenderer::renderClearedFrame(const VulkanSwapchain& swapchain, VkQueue queue, VkClearColorValue clearColor)
{
    return renderFrameImpl(swapchain, queue, clearColor, nullptr, nullptr);
}

std::expected<FrameStatus, std::string>
FrameRenderer::renderFrame(const VulkanSwapchain& swapchain, VkQueue queue, VkClearColorValue clearColor,
                           const FrameRecordFn& record, const OffscreenSceneTarget* target)
{
    return renderFrameImpl(swapchain, queue, clearColor, &record, target);
}

} // namespace starforge::vulkan
