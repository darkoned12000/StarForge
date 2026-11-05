#include "vulkan/threaded_command_pools.hpp"

#include "vulkan/vulkan_result.hpp"

namespace starforge::vulkan
{

ThreadedCommandPools::~ThreadedCommandPools()
{
    destroy();
}

ThreadedCommandPools::ThreadedCommandPools(ThreadedCommandPools&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      threadCount_(std::exchange(other.threadCount_, 0)),
      framesInFlight_(std::exchange(other.framesInFlight_, 0)),
      pools_(std::move(other.pools_))
{
}

ThreadedCommandPools& ThreadedCommandPools::operator=(ThreadedCommandPools&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        threadCount_ = std::exchange(other.threadCount_, 0);
        framesInFlight_ = std::exchange(other.framesInFlight_, 0);
        pools_ = std::move(other.pools_);
    }
    return *this;
}

std::expected<void, std::string> ThreadedCommandPools::init(
    VkDevice device,
    std::uint32_t queueFamily,
    std::uint32_t threadCount,
    std::uint32_t framesInFlight)
{
    device_ = device;
    threadCount_ = std::max(1u, threadCount);
    framesInFlight_ = std::max(1u, framesInFlight);

    pools_.resize(static_cast<std::size_t>(threadCount_) * framesInFlight_, VK_NULL_HANDLE);

    for (std::uint32_t t = 0; t < threadCount_; ++t)
    {
        for (std::uint32_t f = 0; f < framesInFlight_; ++f)
        {
            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.queueFamilyIndex = queueFamily;
            poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

            VkCommandPool pool = VK_NULL_HANDLE;
            if (const VkResult result = vkCreateCommandPool(device, &poolInfo, nullptr, &pool);
                result != VK_SUCCESS)
            {
                destroy();
                return std::unexpected(describeFailure("vkCreateCommandPool", result));
            }
            pools_[static_cast<std::size_t>(t) * framesInFlight_ + f] = pool;
        }
    }
    return {};
}

VkCommandPool ThreadedCommandPools::pool(std::uint32_t threadIndex, std::uint32_t frameIndex) const noexcept
{
    if (threadIndex >= threadCount_ || frameIndex >= framesInFlight_) return VK_NULL_HANDLE;
    return pools_[static_cast<std::size_t>(threadIndex) * framesInFlight_ + frameIndex];
}

void ThreadedCommandPools::resetFrame(std::uint32_t frameIndex) noexcept
{
    if (frameIndex >= framesInFlight_) return;
    for (std::uint32_t t = 0; t < threadCount_; ++t)
    {
        VkCommandPool p = pools_[static_cast<std::size_t>(t) * framesInFlight_ + frameIndex];
        if (p != VK_NULL_HANDLE)
        {
            vkResetCommandPool(device_, p, 0);
        }
    }
}

void ThreadedCommandPools::destroy() noexcept
{
    if (device_ == VK_NULL_HANDLE) return;
    for (auto& p : pools_)
    {
        if (p != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(device_, p, nullptr);
            p = VK_NULL_HANDLE;
        }
    }
    pools_.clear();
    device_ = VK_NULL_HANDLE;
    threadCount_ = 0;
    framesInFlight_ = 0;
}

} // namespace starforge::vulkan
