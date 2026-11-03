#pragma once

#include "vulkan/vulkan_capabilities.hpp"

#include <vulkan/vulkan.h>

#include <expected>
#include <string>
#include <string_view>

namespace starforge::vulkan
{

struct PhysicalDeviceSelection
{
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    GpuCapabilities  capabilities;
};

// Inspects one physical device (properties, queues, features, extensions).
[[nodiscard]] GpuCapabilities queryCapabilities(VkPhysicalDevice device);

// Enumerates all GPUs, logs each with its capabilities, rejects those that miss
// hard requirements and returns the best-scoring remainder. Optional features
// named in `disabledFeatures` are switched off before logging and scoring.
// Presentation support is verified later by VulkanDevice, once a surface exists.
[[nodiscard]] std::expected<PhysicalDeviceSelection, std::string>
selectPhysicalDevice(VkInstance instance, std::string_view disabledFeatures = {});

} // namespace starforge::vulkan
