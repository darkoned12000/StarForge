#include "vulkan/vulkan_capabilities.hpp"

#include "vulkan/vulkan_version.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <format>

namespace starforge::vulkan
{

namespace
{

constexpr std::uint32_t kVendorNvidia = 0x10DE;

constexpr OptionalFeature kOptionalFeatures[] = {
    {"bindless",          &GpuCapabilities::bindless},
    {"drawIndirectCount", &GpuCapabilities::drawIndirectCount},
    {"multiDrawIndirect", &GpuCapabilities::multiDrawIndirect},
    {"shaderFloat64",     &GpuCapabilities::shaderFloat64},
    {"meshShader",        &GpuCapabilities::meshShader},
};

std::string_view trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
    {
        text.remove_suffix(1);
    }
    return text;
}

std::string toLower(std::string_view text)
{
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

} // namespace

std::span<const OptionalFeature> optionalFeatures()
{
    return kOptionalFeatures;
}

std::string disabledFeaturesFromEnvironment()
{
    const char* value = std::getenv(kDisableFeaturesEnvVar);
    return value ? std::string(value) : std::string();
}

FeatureOverrideResult applyFeatureOverrides(GpuCapabilities& caps, std::string_view disableList)
{
    FeatureOverrideResult result;

    std::size_t start = 0;
    while (start <= disableList.size())
    {
        const std::size_t stop = std::min(disableList.find(',', start), disableList.size());
        const std::string_view token = trim(disableList.substr(start, stop - start));
        start = stop + 1;

        if (token.empty())
        {
            continue;
        }

        const std::string wanted = toLower(token);
        const auto it = std::ranges::find_if(kOptionalFeatures, [&wanted](const OptionalFeature& f) {
            return toLower(f.name) == wanted;
        });

        if (it == std::end(kOptionalFeatures))
        {
            result.unknown.emplace_back(token);
            continue;
        }

        if (caps.*(it->member))
        {
            caps.*(it->member) = false;
            result.disabled.emplace_back(it->name);
        }
    }

    return result;
}

std::string driverVersionString(std::uint32_t vendorId, std::uint32_t driverVersion)
{
    if (vendorId == kVendorNvidia)
    {
        // NVIDIA packs major:10 minor:8 patch:8 build:6.
        return std::format("{}.{}.{:02}",
            (driverVersion >> 22) & 0x3FFU,
            (driverVersion >> 14) & 0xFFU,
            (driverVersion >> 6) & 0xFFU);
    }

    // Vulkan standard packing (Mesa/RADV/ANV): major:10 minor:10 patch:12.
    return std::format("{}.{}.{}",
        (driverVersion >> 22) & 0x3FFU,
        (driverVersion >> 12) & 0x3FFU,
        driverVersion & 0xFFFU);
}

std::string missingRequirement(const GpuCapabilities& caps)
{
    if (!meetsMinimumApiVersion(caps.apiVersion))
    {
        return std::format("device API {} is below required {}",
            apiVersionString(caps.apiVersion), apiVersionString(kMinimumApiVersion));
    }
    if (!caps.hasGraphicsQueue)
    {
        return "no graphics queue family";
    }
    if (!caps.dynamicRendering)
    {
        return "dynamicRendering not supported";
    }
    if (!caps.synchronization2)
    {
        return "synchronization2 not supported";
    }
    if (!caps.bufferDeviceAddress)
    {
        return "bufferDeviceAddress not supported";
    }
    if (!caps.shaderInt64)
    {
        return "shaderInt64 not supported";
    }
    if (!caps.shaderDrawParameters)
    {
        return "shaderDrawParameters not supported";
    }
    return {};
}

int scoreDevice(const GpuCapabilities& caps)
{
    int score = 0;

    switch (caps.deviceType)
    {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   score += 1000; break;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score += 100;  break;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    score += 10;   break;
    default: break;
    }

    constexpr int kOptionalFeatureBonus = 50;
    for (const OptionalFeature& feature : kOptionalFeatures)
    {
        if (caps.*(feature.member))
        {
            score += kOptionalFeatureBonus;
        }
    }

    return score;
}

} // namespace starforge::vulkan
