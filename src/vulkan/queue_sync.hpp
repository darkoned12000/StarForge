#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace starforge::vulkan
{

// Host synchronization for vkQueueSubmit2 (Vulkan requires external sync).
// All submissions in the process funnel through this mutex so background
// loaders and the render thread can safely share the graphics queue (M5 2.2).
[[nodiscard]] VkResult submit2(VkQueue queue,
                               std::uint32_t submitCount,
                               const VkSubmitInfo2* submits,
                               VkFence fence);

} // namespace starforge::vulkan
