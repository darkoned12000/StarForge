#include "vulkan/queue_sync.hpp"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace starforge::vulkan
{

namespace
{
std::mutex& mutexForQueue(VkQueue queue)
{
    static std::mutex g_mapMutex;
    static std::unordered_map<VkQueue, std::unique_ptr<std::mutex>> g_mutexes;
    std::lock_guard lock(g_mapMutex);
    auto [it, inserted] = g_mutexes.try_emplace(queue, nullptr);
    if (inserted)
    {
        it->second = std::make_unique<std::mutex>();
    }
    return *it->second;
}
}

VkResult submit2(VkQueue queue,
                 std::uint32_t submitCount,
                 const VkSubmitInfo2* submits,
                 VkFence fence)
{
    std::lock_guard lock(mutexForQueue(queue));
    return vkQueueSubmit2(queue, submitCount, submits, fence);
}

VkResult present(VkQueue queue, const VkPresentInfoKHR* presentInfo)
{
    std::lock_guard lock(mutexForQueue(queue));
    return vkQueuePresentKHR(queue, presentInfo);
}

} // namespace starforge::vulkan
