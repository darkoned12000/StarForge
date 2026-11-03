#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <format>
#include <string>

namespace starforge::vulkan
{

// Hard floor: Vulkan 1.3 core provides dynamic rendering, synchronization2
// and buffer device address, which the backend is built around.
inline constexpr std::uint32_t kMinimumApiVersion = VK_API_VERSION_1_3;

// Requested when the loader and driver support it.
inline constexpr std::uint32_t kPreferredApiVersion = VK_API_VERSION_1_4;

[[nodiscard]] constexpr bool meetsMinimumApiVersion(std::uint32_t version) noexcept
{
    return version >= kMinimumApiVersion;
}

[[nodiscard]] inline std::string apiVersionString(std::uint32_t version)
{
    return std::format("{}.{}.{}",
        VK_API_VERSION_MAJOR(version),
        VK_API_VERSION_MINOR(version),
        VK_API_VERSION_PATCH(version));
}

} // namespace starforge::vulkan
