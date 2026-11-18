#include "vulkan/queue_sync.hpp"

#include <mutex>

namespace starforge::vulkan
{

namespace
{
std::mutex g_submitMutex;
}

VkResult submit2(VkQueue queue,
                 std::uint32_t submitCount,
                 const VkSubmitInfo2* submits,
                 VkFence fence)
{
    std::lock_guard lock(g_submitMutex);
    return vkQueueSubmit2(queue, submitCount, submits, fence);
}

} // namespace starforge::vulkan
