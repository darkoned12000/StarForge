#include "gpu_resources/transfer.hpp"

#include "vulkan/vulkan_result.hpp"

#include <utility>

namespace starforge::gpu_resources
{

std::expected<Uploader, std::string> Uploader::create(const starforge::vulkan::VulkanDevice& device)
{
    Uploader uploader;
    uploader.owner_ = &device;
    uploader.device_ = device.device();
    uploader.queueFamily_ = device.graphicsQueueFamily();

    if (uploader.device_ == VK_NULL_HANDLE || device.graphicsQueue() == VK_NULL_HANDLE)
    {
        return std::unexpected("Invalid device/queue for uploader");
    }

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = uploader.queueFamily_;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (const VkResult result = vkCreateCommandPool(uploader.device_, &poolInfo, nullptr, &uploader.commandPool_);
        result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkCreateCommandPool", result));
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = uploader.commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    if (const VkResult result = vkAllocateCommandBuffers(uploader.device_, &allocInfo, &uploader.commandBuffer_);
        result != VK_SUCCESS)
    {
        uploader.destroy();
        return std::unexpected(starforge::vulkan::describeFailure("vkAllocateCommandBuffers", result));
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (const VkResult result = vkCreateFence(uploader.device_, &fenceInfo, nullptr, &uploader.fence_);
        result != VK_SUCCESS)
    {
        uploader.destroy();
        return std::unexpected(starforge::vulkan::describeFailure("vkCreateFence", result));
    }

    return uploader;
}

Uploader::~Uploader()
{
    destroy();
}

Uploader::Uploader(Uploader&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)),
      device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      commandPool_(std::exchange(other.commandPool_, VK_NULL_HANDLE)),
      commandBuffer_(std::exchange(other.commandBuffer_, VK_NULL_HANDLE)),
      fence_(std::exchange(other.fence_, VK_NULL_HANDLE)),
      queueFamily_(other.queueFamily_),
      operationCount_(std::exchange(other.operationCount_, 0))
{
}

Uploader& Uploader::operator=(Uploader&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        owner_ = std::exchange(other.owner_, nullptr);
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        commandPool_ = std::exchange(other.commandPool_, VK_NULL_HANDLE);
        commandBuffer_ = std::exchange(other.commandBuffer_, VK_NULL_HANDLE);
        fence_ = std::exchange(other.fence_, VK_NULL_HANDLE);
        queueFamily_ = other.queueFamily_;
        operationCount_ = std::exchange(other.operationCount_, 0);
    }
    return *this;
}

void Uploader::destroy() noexcept
{
    if (device_ != VK_NULL_HANDLE)
    {
        if (fence_ != VK_NULL_HANDLE) vkDestroyFence(device_, fence_, nullptr);
        // Destroying the pool implicitly frees its command buffers.
        if (commandPool_ != VK_NULL_HANDLE) vkDestroyCommandPool(device_, commandPool_, nullptr);
    }
    owner_ = nullptr;
    device_ = VK_NULL_HANDLE;
    commandPool_ = VK_NULL_HANDLE;
    commandBuffer_ = VK_NULL_HANDLE;
    fence_ = VK_NULL_HANDLE;
    queueFamily_ = 0;
    operationCount_ = 0;
}

std::expected<void, std::string> Uploader::copyBuffer(const Buffer& src, Buffer& dst, VkDeviceSize size, VkDeviceSize srcOffset, VkDeviceSize dstOffset)
{
    std::lock_guard lock(mutex_);

    if (owner_ == nullptr || device_ == VK_NULL_HANDLE || commandPool_ == VK_NULL_HANDLE ||
        commandBuffer_ == VK_NULL_HANDLE || fence_ == VK_NULL_HANDLE)
    {
        return std::unexpected("Uploader not initialized");
    }
    if (size == 0)
    {
        return std::unexpected("copyBuffer size must be > 0");
    }
    if (srcOffset > src.size() || size > src.size() - srcOffset ||
        dstOffset > dst.size() || size > dst.size() - dstOffset)
    {
        return std::unexpected("copyBuffer range exceeds buffer bounds");
    }

    if (const VkResult result = vkResetFences(device_, 1, &fence_); result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkResetFences", result));
    }
    if (const VkResult result = vkResetCommandBuffer(commandBuffer_, 0); result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkResetCommandBuffer", result));
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (const VkResult result = vkBeginCommandBuffer(commandBuffer_, &beginInfo); result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkBeginCommandBuffer", result));
    }

    VkBufferMemoryBarrier2 preBarrier{};
    preBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    preBarrier.srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    preBarrier.srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
    preBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    preBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    preBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    preBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    preBarrier.buffer = src.handle();
    preBarrier.offset = srcOffset;
    preBarrier.size = size;

    VkDependencyInfo preDep{};
    preDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    preDep.bufferMemoryBarrierCount = 1;
    preDep.pBufferMemoryBarriers = &preBarrier;
    vkCmdPipelineBarrier2(commandBuffer_, &preDep);

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer_, src.handle(), dst.handle(), 1, &copyRegion);

    VkBufferMemoryBarrier2 postBarrier{};
    postBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    postBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    postBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    postBarrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    postBarrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT;
    postBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    postBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    postBarrier.buffer = dst.handle();
    postBarrier.offset = dstOffset;
    postBarrier.size = size;

    VkDependencyInfo postDep{};
    postDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    postDep.bufferMemoryBarrierCount = 1;
    postDep.pBufferMemoryBarriers = &postBarrier;
    vkCmdPipelineBarrier2(commandBuffer_, &postDep);

    if (const VkResult result = vkEndCommandBuffer(commandBuffer_); result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkEndCommandBuffer", result));
    }

    VkCommandBufferSubmitInfo cmdSubmit{};
    cmdSubmit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmit.commandBuffer = commandBuffer_;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdSubmit;

    if (const VkResult result = owner_->submit(1, &submitInfo, fence_); result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkQueueSubmit2", result));
    }
    if (const VkResult result = vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX); result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkWaitForFences", result));
    }

    ++operationCount_;
    return {};
}

} // namespace starforge::gpu_resources
