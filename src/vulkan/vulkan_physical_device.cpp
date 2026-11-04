#include "vulkan/vulkan_physical_device.hpp"

#include "core/log.hpp"
#include "vulkan/vulkan_result.hpp"
#include "vulkan/vulkan_version.hpp"

#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace starforge::vulkan
{

namespace
{

constexpr const char* kTag = "VULKAN";

bool hasExtension(const std::vector<VkExtensionProperties>& extensions, const char* name)
{
    for (const auto& extension : extensions)
    {
        if (std::strcmp(extension.extensionName, name) == 0)
        {
            return true;
        }
    }
    return false;
}

bool findGraphicsQueue(VkPhysicalDevice device)
{
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);

    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    for (const auto& family : families)
    {
        if (family.queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            return true;
        }
    }
    return false;
}

void reportOverrides(const FeatureOverrideResult& result)
{
    if (!result.disabled.empty())
    {
        std::string names;
        for (const std::string& name : result.disabled)
        {
            names += names.empty() ? name : ", " + name;
        }
        core::logWarning(kTag, "  feature override ({}): disabled {}", kDisableFeaturesEnvVar, names);
    }

    if (!result.unknown.empty())
    {
        std::string valid;
        for (const OptionalFeature& feature : optionalFeatures())
        {
            valid += valid.empty() ? std::string(feature.name) : ", " + std::string(feature.name);
        }
        for (const std::string& name : result.unknown)
        {
            core::logWarning(kTag, "  feature override: '{}' ignored (overridable: {})", name, valid);
        }
    }
}

void logCapabilities(const GpuCapabilities& caps)
{
    core::logInfo(kTag, "GPU: {}", caps.deviceName);
    core::logInfo(kTag, "  API {}, driver {} (raw 0x{:08x})",
        apiVersionString(caps.apiVersion),
        driverVersionString(caps.vendorId, caps.driverVersion), caps.driverVersion);
    core::logInfo(kTag,
        "  required: dynamicRendering={} synchronization2={} bufferDeviceAddress={}",
        caps.dynamicRendering, caps.synchronization2, caps.bufferDeviceAddress);
    core::logInfo(kTag,
        "  optional: bindless={} drawIndirectCount={} multiDrawIndirect={} "
        "shaderFloat64={} meshShader={}",
        caps.bindless, caps.drawIndirectCount, caps.multiDrawIndirect,
        caps.shaderFloat64, caps.meshShader);
}

} // namespace

GpuCapabilities queryCapabilities(VkPhysicalDevice device)
{
    GpuCapabilities caps;

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);

    caps.deviceName = properties.deviceName;
    caps.vendorId = properties.vendorID;
    caps.deviceType = properties.deviceType;
    caps.apiVersion = properties.apiVersion;
    caps.driverVersion = properties.driverVersion;
    caps.hasGraphicsQueue = findGraphicsQueue(device);

    // Feature structs for 1.2/1.3 are only valid to query on 1.3+ devices.
    if (!meetsMinimumApiVersion(properties.apiVersion))
    {
        return caps;
    }

    std::uint32_t extensionCount = 0;
    if (const VkResult result = vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
        result != VK_SUCCESS)
    {
        core::logWarning(kTag, "Failed to enumerate device extension count: {}", describeFailure("vkEnumerateDeviceExtensionProperties", result));
        return caps;
    }

    std::vector<VkExtensionProperties> extensions(extensionCount);
    VkResult result = vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());
    if (result != VK_SUCCESS && result != VK_INCOMPLETE)
    {
        core::logWarning(kTag, "Failed to enumerate device extensions: {}", describeFailure("vkEnumerateDeviceExtensionProperties", result));
        return caps;
    }
    if (result == VK_INCOMPLETE)
    {
        extensions.resize(extensionCount);
    }
    else
    {
        extensions.resize(extensionCount);
    }

    const bool meshExtension = hasExtension(extensions, VK_EXT_MESH_SHADER_EXTENSION_NAME);

    VkPhysicalDeviceMeshShaderFeaturesEXT meshFeatures{};
    meshFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;

    VkPhysicalDeviceVulkan11Features features11{};
    features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.pNext = meshExtension ? &meshFeatures : nullptr;

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.pNext = &features12;

    features11.pNext = &features13;

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &features11;

    vkGetPhysicalDeviceFeatures2(device, &features2);

    caps.dynamicRendering = features13.dynamicRendering == VK_TRUE;
    caps.synchronization2 = features13.synchronization2 == VK_TRUE;
    caps.bufferDeviceAddress = features12.bufferDeviceAddress == VK_TRUE;
    caps.shaderInt64 = features2.features.shaderInt64 == VK_TRUE;
    caps.shaderDrawParameters = features11.shaderDrawParameters == VK_TRUE;

    caps.bindless =
        features12.runtimeDescriptorArray == VK_TRUE &&
        features12.descriptorBindingPartiallyBound == VK_TRUE &&
        features12.descriptorBindingVariableDescriptorCount == VK_TRUE &&
        features12.shaderSampledImageArrayNonUniformIndexing == VK_TRUE;
    caps.drawIndirectCount = features12.drawIndirectCount == VK_TRUE;
    caps.multiDrawIndirect = features2.features.multiDrawIndirect == VK_TRUE;
    caps.shaderFloat64 = features2.features.shaderFloat64 == VK_TRUE;
    caps.meshShader = meshExtension && meshFeatures.meshShader == VK_TRUE;

    return caps;
}

std::expected<PhysicalDeviceSelection, std::string> selectPhysicalDevice(VkInstance instance, std::string_view disabledFeatures)
{
    std::uint32_t count = 0;
    if (const VkResult result = vkEnumeratePhysicalDevices(instance, &count, nullptr);
        result != VK_SUCCESS)
    {
        return std::unexpected(describeFailure("vkEnumeratePhysicalDevices", result));
    }

    if (count == 0)
    {
        return std::unexpected("No Vulkan-compatible GPU detected");
    }

    std::vector<VkPhysicalDevice> devices(count);
    if (const VkResult result = vkEnumeratePhysicalDevices(instance, &count, devices.data());
        result != VK_SUCCESS && result != VK_INCOMPLETE)
    {
        return std::unexpected(describeFailure("vkEnumeratePhysicalDevices", result));
    }
    devices.resize(count);

    PhysicalDeviceSelection best;
    int bestScore = -1;

    for (const VkPhysicalDevice device : devices)
    {
        GpuCapabilities caps = queryCapabilities(device);
        logCapabilities(caps);

        if (!disabledFeatures.empty())
        {
            reportOverrides(applyFeatureOverrides(caps, disabledFeatures));
            core::logInfo(kTag, "  effective optional: bindless={} drawIndirectCount={} "
                "multiDrawIndirect={} shaderFloat64={} meshShader={}",
                caps.bindless, caps.drawIndirectCount, caps.multiDrawIndirect,
                caps.shaderFloat64, caps.meshShader);
        }

        if (const std::string reason = missingRequirement(caps); !reason.empty())
        {
            core::logWarning(kTag, "  rejected: {}", reason);
            continue;
        }

        const int score = scoreDevice(caps);
        core::logInfo(kTag, "  accepted, score {}", score);

        if (score > bestScore)
        {
            bestScore = score;
            best.physicalDevice = device;
            best.capabilities = std::move(caps);
        }
    }

    if (best.physicalDevice == VK_NULL_HANDLE)
    {
        return std::unexpected(
            "No GPU meets the minimum requirements (Vulkan 1.3, dynamic rendering, "
            "synchronization2, buffer device address)");
    }

    core::logInfo(kTag, "Selected GPU: {}", best.capabilities.deviceName);
    return best;
}

} // namespace starforge::vulkan
