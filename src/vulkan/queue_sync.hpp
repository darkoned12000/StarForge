#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace starforge::vulkan
{

// Host synchronization for Vulkan queue operations (external sync required).
// All submissions/presents in the process funnel through this mutex so
// background loaders and the render thread can safely share the graphics queue.
[[nodiscard]] VkResult submit2(VkQueue queue,
                               std::uint32_t submitCount,
                               const VkSubmitInfo2* submits,
                               VkFence fence);

[[nodiscard]] VkResult present(VkQueue queue, const VkPresentInfoKHR* presentInfo);

} // namespace starforge::vulkan
