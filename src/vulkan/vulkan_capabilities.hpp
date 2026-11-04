#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace starforge::vulkan
{

// What a physical device can do. Systems branch on this instead of assuming
// hardware features, which gives every optional feature a fallback path.
struct GpuCapabilities
{
    std::string   deviceName;
    VkPhysicalDeviceType deviceType = VK_PHYSICAL_DEVICE_TYPE_OTHER;
    std::uint32_t apiVersion = 0;
    std::uint32_t vendorId = 0;
    std::uint32_t driverVersion = 0;

    // Required (Vulkan 1.3 core).
    bool dynamicRendering = false;
    bool synchronization2 = false;
    bool bufferDeviceAddress = false;
    bool hasGraphicsQueue = false;
    bool shaderInt64 = false;         // Required for BDA (uint64_t addresses in shaders)
    bool shaderDrawParameters = false; // Vulkan 1.1 core feature

    // Optional; each needs a fallback before a system may depend on it.
    bool bindless = false;          // descriptor indexing, partially bound, runtime arrays
    bool drawIndirectCount = false; // GPU-driven draw generation
    bool multiDrawIndirect = false;
    bool shaderFloat64 = false;     // double precision in shaders
    bool meshShader = false;        // VK_EXT_mesh_shader
};

// An optional feature that can be listed by name (e.g. for overrides).
struct OptionalFeature
{
    std::string_view name;
    bool GpuCapabilities::* member;
};

[[nodiscard]] std::span<const OptionalFeature> optionalFeatures();

// Environment variable holding a comma-separated list of optional features to
// treat as unavailable, e.g. STARFORGE_DISABLE_FEATURES=meshShader,bindless
inline constexpr const char* kDisableFeaturesEnvVar = "STARFORGE_DISABLE_FEATURES";

// Value of kDisableFeaturesEnvVar, or an empty string when unset.
[[nodiscard]] std::string disabledFeaturesFromEnvironment();

struct FeatureOverrideResult
{
    std::vector<std::string> disabled; // features that were available and are now off
    std::vector<std::string> unknown;  // names that are not overridable optional features
};

// Turns off the named optional features (case-insensitive, comma-separated,
// whitespace ignored). Hard requirements cannot be overridden: they are
// reported as unknown. Used to exercise fallback paths on capable hardware.
FeatureOverrideResult applyFeatureOverrides(GpuCapabilities& caps, std::string_view disableList);

// Human-readable driver version. The packing of VkPhysicalDeviceProperties::
// driverVersion is vendor-specific (NVIDIA differs from the Vulkan standard
// layout used by Mesa/RADV).
[[nodiscard]] std::string driverVersionString(std::uint32_t vendorId, std::uint32_t driverVersion);

// Empty string when the device satisfies every hard requirement, otherwise a
// human-readable description of the first missing requirement.
[[nodiscard]] std::string missingRequirement(const GpuCapabilities& caps);

// Higher is better. Only meaningful for devices that meet the requirements.
[[nodiscard]] int scoreDevice(const GpuCapabilities& caps);

} // namespace starforge::vulkan
