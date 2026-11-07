#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <functional>
#include <vector>

namespace starforge::vulkan
{

// GPU-timeline deferred retirement queue (M7.2 / 5.5.3c): resources are
// retired with the fence that signals GPU completion; deleters run only
// after that fence is signaled. This lets per-frame resources be freed
// safely without a CPU-side epoch delay.
//
// Not thread-safe: call retire()/collect() from a single thread
// (typically the render/main thread, after waiting on frame fences).
class GpuRetirementQueue
{
public:
    GpuRetirementQueue() = default;

    void retire(VkFence fence, std::function<void()> deleter);

    // Run deleters whose fences have signaled; retirees whose fences are
    // still pending stay queued. Returns the number collected.
    std::size_t collect(VkDevice device);

    [[nodiscard]] std::size_t pending() const noexcept { return entries_.size(); }

private:
    struct Entry
    {
        VkFence fence = VK_NULL_HANDLE;
        std::function<void()> deleter;
    };
    std::vector<Entry> entries_;
};

} // namespace starforge::vulkan
