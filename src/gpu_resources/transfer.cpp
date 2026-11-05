#include "gpu_resources/transfer.hpp"

#include "vulkan/vulkan_result.hpp"

#include <utility>

namespace starforge::gpu_resources
{

std::expected<Uploader, std::string> Uploader::create(const starforge::vulkan::VulkanDevice& device)
{
    Uploader uploader;
    uploader.device_ = device.device();
    uploader.queueFamily_ = device.graphicsQueueFamily();
    uploader.queue_ = device.graphicsQueue();

    if (uploader.device_ == VK_NULL_HANDLE || uploader.queue_ == VK_NULL_HANDLE)
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

    return uploader;
}

Uploader::~Uploader()
{
    destroy();
}

Uploader::Uploader(Uploader&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      commandPool_(std::exchange(other.commandPool_, VK_NULL_HANDLE)),
      queueFamily_(other.queueFamily_),
      queue_(std::exchange(other.queue_, VK_NULL_HANDLE))
{
}

Uploader& Uploader::operator=(Uploader&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        commandPool_ = std::exchange(other.commandPool_, VK_NULL_HANDLE);
        queueFamily_ = other.queueFamily_;
        queue_ = std::exchange(other.queue_, VK_NULL_HANDLE);
    }
    return *this;
}

void Uploader::destroy() noexcept
{
    if (device_ != VK_NULL_HANDLE && commandPool_ != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
    }
    device_ = VK_NULL_HANDLE;
    commandPool_ = VK_NULL_HANDLE;
    queueFamily_ = 0;
    queue_ = VK_NULL_HANDLE;
}

std::expected<void, std::string> Uploader::copyBuffer(const Buffer& src, Buffer& dst, VkDeviceSize size, VkDeviceSize srcOffset, VkDeviceSize dstOffset)
{
    if (device_ == VK_NULL_HANDLE || commandPool_ == VK_NULL_HANDLE || queue_ == VK_NULL_HANDLE)
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

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (const VkResult result = vkAllocateCommandBuffers(device_, &allocInfo, &cmd);
        result != VK_SUCCESS)
    {
        return std::unexpected(starforge::vulkan::describeFailure("vkAllocateCommandBuffers", result));
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (const VkResult result = vkBeginCommandBuffer(cmd, &beginInfo); result != VK_SUCCESS)
    {
        vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
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
    vkCmdPipelineBarrier2(cmd, &preDep);

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size;
    vkCmdCopyBuffer(cmd, src.handle(), dst.handle(), 1, &copyRegion);

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
    vkCmdPipelineBarrier2(cmd, &postDep);

    if (const VkResult result = vkEndCommandBuffer(cmd); result != VK_SUCCESS)
    {
        vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
        return std::unexpected(starforge::vulkan::describeFailure("vkEndCommandBuffer", result));
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    if (const VkResult result = vkCreateFence(device_, &fenceInfo, nullptr, &fence); result != VK_SUCCESS)
    {
        vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
        return std::unexpected(starforge::vulkan::describeFailure("vkCreateFence", result));
    }

    VkCommandBufferSubmitInfo cmdSubmit{};
    cmdSubmit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmit.commandBuffer = cmd;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdSubmit;

    if (const VkResult result = vkQueueSubmit2(queue_, 1, &submitInfo, fence); result != VK_SUCCESS)
    {
        vkDestroyFence(device_, fence, nullptr);
        vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
        return std::unexpected(starforge::vulkan::describeFailure("vkQueueSubmit2", result));
    }

    if (const VkResult result = vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX); result != VK_SUCCESS)
    {
        vkDestroyFence(device_, fence, nullptr);
        vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
        return std::unexpected(starforge::vulkan::describeFailure("vkWaitForFences", result));
    }

    vkDestroyFence(device_, fence, nullptr);
    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
    return {};
}

} // namespace starforge::gpu_resources
