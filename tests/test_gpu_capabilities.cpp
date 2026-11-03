#include <doctest/doctest.h>

#include "vulkan/vulkan_capabilities.hpp"

using namespace starforge::vulkan;

namespace
{

GpuCapabilities capableDevice()
{
    GpuCapabilities caps;
    caps.deviceName = "Test GPU";
    caps.deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    caps.apiVersion = VK_API_VERSION_1_4;
    caps.hasGraphicsQueue = true;
    caps.dynamicRendering = true;
    caps.synchronization2 = true;
    caps.bufferDeviceAddress = true;
    return caps;
}

} // namespace

TEST_CASE("a device with all required features is accepted")
{
    CHECK(missingRequirement(capableDevice()).empty());
}

TEST_CASE("each missing hard requirement is reported")
{
    SUBCASE("old API")
    {
        auto caps = capableDevice();
        caps.apiVersion = VK_API_VERSION_1_2;
        CHECK_FALSE(missingRequirement(caps).empty());
    }
    SUBCASE("no graphics queue")
    {
        auto caps = capableDevice();
        caps.hasGraphicsQueue = false;
        CHECK_FALSE(missingRequirement(caps).empty());
    }
    SUBCASE("no dynamic rendering")
    {
        auto caps = capableDevice();
        caps.dynamicRendering = false;
        CHECK_FALSE(missingRequirement(caps).empty());
    }
    SUBCASE("no synchronization2")
    {
        auto caps = capableDevice();
        caps.synchronization2 = false;
        CHECK_FALSE(missingRequirement(caps).empty());
    }
    SUBCASE("no buffer device address")
    {
        auto caps = capableDevice();
        caps.bufferDeviceAddress = false;
        CHECK_FALSE(missingRequirement(caps).empty());
    }
}

TEST_CASE("optional features never cause rejection")
{
    auto caps = capableDevice();
    caps.meshShader = false;
    caps.bindless = false;
    CHECK(missingRequirement(caps).empty());
}

TEST_CASE("scoring prefers discrete GPUs, then optional features")
{
    auto discrete = capableDevice();

    auto integrated = capableDevice();
    integrated.deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    integrated.meshShader = true;
    integrated.bindless = true;

    CHECK(scoreDevice(discrete) > scoreDevice(integrated));

    auto richer = capableDevice();
    richer.meshShader = true;
    CHECK(scoreDevice(richer) > scoreDevice(discrete));
}

namespace
{

GpuCapabilities fullyFeaturedDevice()
{
    auto caps = capableDevice();
    caps.bindless = true;
    caps.drawIndirectCount = true;
    caps.multiDrawIndirect = true;
    caps.shaderFloat64 = true;
    caps.meshShader = true;
    return caps;
}

} // namespace

TEST_CASE("feature overrides disable named optional features")
{
    auto caps = fullyFeaturedDevice();
    const auto result = applyFeatureOverrides(caps, "meshShader,bindless");

    CHECK_FALSE(caps.meshShader);
    CHECK_FALSE(caps.bindless);
    CHECK(caps.drawIndirectCount);
    CHECK(result.disabled.size() == 2);
    CHECK(result.unknown.empty());
}

TEST_CASE("feature overrides are case-insensitive and ignore whitespace")
{
    auto caps = fullyFeaturedDevice();
    applyFeatureOverrides(caps, "  MESHSHADER , ShaderFloat64 ,, ");

    CHECK_FALSE(caps.meshShader);
    CHECK_FALSE(caps.shaderFloat64);
    CHECK(caps.bindless);
}

TEST_CASE("unknown and required feature names are reported, not applied")
{
    auto caps = fullyFeaturedDevice();
    const auto result = applyFeatureOverrides(caps, "dynamicRendering,nonsense");

    CHECK(caps.dynamicRendering);
    CHECK(result.disabled.empty());
    CHECK(result.unknown.size() == 2);
}

TEST_CASE("empty override list changes nothing")
{
    auto caps = fullyFeaturedDevice();
    const auto result = applyFeatureOverrides(caps, "");

    CHECK(caps.meshShader);
    CHECK(result.disabled.empty());
    CHECK(result.unknown.empty());
}

TEST_CASE("overriding features lowers the device score")
{
    auto caps = fullyFeaturedDevice();
    const int before = scoreDevice(caps);
    applyFeatureOverrides(caps, "meshShader");

    CHECK(scoreDevice(caps) < before);
}

TEST_CASE("driver version decoding is vendor-aware")
{
    constexpr std::uint32_t kAmd = 0x1002;
    constexpr std::uint32_t kNvidia = 0x10DE;

    // Raw value reported by RADV on the RX 7900 XTX.
    CHECK(driverVersionString(kAmd, 0x06802006) == "26.2.6");

    // NVIDIA 560.35.03 packed as 10:8:8:6.
    const std::uint32_t nvidia = (560U << 22) | (35U << 14) | (3U << 6);
    CHECK(driverVersionString(kNvidia, nvidia) == "560.35.03");
}
