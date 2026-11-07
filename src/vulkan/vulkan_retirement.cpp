#include "vulkan/vulkan_retirement.hpp"

#include "core/log.hpp"

#include <utility>

namespace starforge::vulkan
{

void GpuRetirementQueue::retire(VkFence fence, std::function<void()> deleter)
{
    entries_.push_back(Entry{fence, std::move(deleter)});
}

std::size_t GpuRetirementQueue::collect(VkDevice device)
{
    std::size_t writeIdx = 0;
    std::size_t collected = 0;
    const std::size_t count = entries_.size();

    // Cache fence queries: retirees against the same frame fence share
    // one status query instead of one per entry.
    VkFence lastFence = VK_NULL_HANDLE;
    VkResult lastStatus = VK_SUCCESS;

    for (std::size_t readIdx = 0; readIdx < count; ++readIdx)
    {
        VkResult status;
        if (entries_[readIdx].fence == VK_NULL_HANDLE)
        {
            status = VK_SUCCESS;
        }
        else if (entries_[readIdx].fence == lastFence)
        {
            status = lastStatus;
        }
        else
        {
            status = vkGetFenceStatus(device, entries_[readIdx].fence);
            lastFence = entries_[readIdx].fence;
            lastStatus = status;
        }

        if (status == VK_SUCCESS)
        {
            if (entries_[readIdx].deleter)
            {
                // Deleters must not escape collect(): run them exactly
                // once and never re-queue a completed deleter.
                try
                {
                    entries_[readIdx].deleter();
                }
                catch (...)
                {
                    core::logError("VULKAN", "GpuRetirementQueue deleter threw; swallowed");
                }
            }
            ++collected;
        }
        else
        {
            if (writeIdx != readIdx)
            {
                entries_[writeIdx] = std::move(entries_[readIdx]);
            }
            ++writeIdx;
        }
    }

    entries_.resize(writeIdx);
    return collected;
}

} // namespace starforge::vulkan
