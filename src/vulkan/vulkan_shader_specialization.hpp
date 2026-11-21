#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace starforge::vulkan
{

// Specialization constant 0 in shaders/graphics/output_shoulder.slang:
// `kHdrOutput`. Vulkan specialization bools are 4-byte values (VkBool32).
// The shaders default to false (soft shoulder for the direct swapchain path);
// the future offscreen RGBA16F path passes specialization(true) so linear HDR
// values reach the tonemap pass untouched.
[[nodiscard]] inline const VkSpecializationInfo& hdrOutputSpecialization(bool enabled)
{
    static const std::uint32_t kOff = 0u;
    static const std::uint32_t kOn = 1u;
    static const VkSpecializationMapEntry kEntry{0u, 0u, sizeof(std::uint32_t)};
    static const VkSpecializationInfo kOffInfo{1u, &kEntry, sizeof(std::uint32_t), &kOff};
    static const VkSpecializationInfo kOnInfo{1u, &kEntry, sizeof(std::uint32_t), &kOn};
    return enabled ? kOnInfo : kOffInfo;
}

} // namespace starforge::vulkan
