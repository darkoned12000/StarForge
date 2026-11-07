#include "vulkan/vulkan_renderer.hpp"

#include "core/log.hpp"
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
}

void recordClearColorImage(VkCommandBuffer cmd,
                           VkImage image,
                           VkImageView imageView,
                           VkExtent2D extent,
                           VkClearColorValue clearColor,
                           VkImageLayout finalLayout,
                           VkPipelineStageFlags2 dstStageMask,
                           VkAccessFlags2 dstAccessMask)
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

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = extent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

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
                               const FrameRecordFn* record)
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
            0);
    }
    else
    {
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
        toAttachment.image = images.at(imageIndex);
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

        VkRenderingAttachmentInfo attachment{};
        attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        attachment.imageView = views.at(imageIndex);
        attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.clearValue.color = clearColor;

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea.offset = {0, 0};
        renderingInfo.renderArea.extent = swapchain.extent();
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &attachment;

        vkCmdBeginRendering(cmd, &renderingInfo);
        (*record)(cmd, swapchain.extent());
        vkCmdEndRendering(cmd);

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

    if (const VkResult result = vkQueueSubmit2(queue, 1, &submitInfo, fence); result != VK_SUCCESS)
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
    return renderFrameImpl(swapchain, queue, clearColor, nullptr);
}

std::expected<FrameStatus, std::string>
FrameRenderer::renderFrame(const VulkanSwapchain& swapchain, VkQueue queue, VkClearColorValue clearColor,
                           const FrameRecordFn& record)
{
    return renderFrameImpl(swapchain, queue, clearColor, &record);
}

} // namespace starforge::vulkan
