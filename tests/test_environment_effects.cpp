#include <doctest/doctest.h>

#include "environment/afterburner_effect.hpp"
#include "environment/asteroid_field.hpp"
#include "environment/collision_effect.hpp"
#include "environment/dust_field.hpp"
#include "environment/ftl_effect.hpp"
#include "environment/nebula_renderer.hpp"
#include "environment/particle_system.hpp"
#include "environment/shield_effect.hpp"
#include "environment/starfield_renderer.hpp"
#include "environment/storm_effect.hpp"
#include "environment/sun_distortion.hpp"
#include "environment/thruster_effect.hpp"
#include "environment/weapon_effect.hpp"
#include "environment/wormhole_effect.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"

#ifndef STARFORGE_SHADER_DIR
#define STARFORGE_SHADER_DIR "shaders"
#endif

using namespace starforge::environment;
using namespace starforge::vulkan;

TEST_CASE("Environment effects: all create via traditional vertex/fragment path (8.9)")
{
    // M8 mesh-shader criterion: no environment effect uses mesh/task
    // shaders, so the traditional vertex/fragment pipeline is the only
    // path — it must create successfully regardless of whether the GPU
    // reports meshShader support (non-mesh fallback == the sole path).
    ContextCreateInfo info;
    info.enableValidation = kValidationDefault;
    info.requiredExtensions.clear();

    auto context = VulkanContext::create(info);
    REQUIRE(context.has_value());

    {
        auto device = VulkanDevice::create(*context);
        REQUIRE(device.has_value());

        const std::string dir = std::string(STARFORGE_SHADER_DIR);
        const auto deviceInfo = device->device();
        const auto phys = context->physicalDevice();
        const auto qf = device->graphicsQueueFamily();

        CHECK(StarfieldRenderer::create({
            .device = deviceInfo, .physicalDevice = phys, .queueFamily = qf,
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM, .shaderDirectory = dir,
        }).has_value());
        CHECK(NebulaRenderer::create({
            .device = deviceInfo, .physicalDevice = phys, .queueFamily = qf,
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM, .shaderDirectory = dir,
        }).has_value());
        CHECK(DustFieldRenderer::create({
            .device = deviceInfo, .physicalDevice = phys, .queueFamily = qf,
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM, .shaderDirectory = dir,
        }).has_value());
        CHECK(ParticleSystem::create({
            .device = deviceInfo, .physicalDevice = phys, .queueFamily = qf,
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM, .shaderDirectory = dir,
        }).has_value());
        CHECK(ThrusterEffect::create({
            .device = deviceInfo, .physicalDevice = phys, .queueFamily = qf,
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM, .shaderDirectory = dir,
        }).has_value());
        CHECK(WeaponEffect::create({
            .device = deviceInfo, .physicalDevice = phys, .queueFamily = qf,
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM, .shaderDirectory = dir,
        }).has_value());
        CHECK(ShieldEffect::create({
            .device = deviceInfo, .physicalDevice = phys, .queueFamily = qf,
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM, .shaderDirectory = dir,
        }).has_value());
        CHECK(CollisionEffect::create({
            .device = deviceInfo, .physicalDevice = phys, .queueFamily = qf,
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM, .shaderDirectory = dir,
        }).has_value());
        CHECK(SunDistortionEffect::create({
            .device = deviceInfo, .physicalDevice = phys, .queueFamily = qf,
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM, .shaderDirectory = dir,
        }).has_value());
        CHECK(WormholeEffect::create({
            .device = deviceInfo, .physicalDevice = phys, .queueFamily = qf,
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM, .shaderDirectory = dir,
        }).has_value());
        CHECK(FtlEffect::create({
            .device = deviceInfo, .physicalDevice = phys, .queueFamily = qf,
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM, .shaderDirectory = dir,
        }).has_value());
        CHECK(AfterburnerEffect::create({
            .device = deviceInfo, .physicalDevice = phys, .queueFamily = qf,
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM, .shaderDirectory = dir,
        }).has_value());
        CHECK(StormEffect::create({
            .device = deviceInfo, .physicalDevice = phys, .queueFamily = qf,
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM, .shaderDirectory = dir,
        }).has_value());
        CHECK(AsteroidFieldRenderer::create({
            .device = deviceInfo, .physicalDevice = phys, .queueFamily = qf,
            .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
            .depthFormat = VK_FORMAT_D32_SFLOAT, .shaderDirectory = dir,
        }).has_value());
    }

    CHECK(context->validationErrorCount() == 0);
}

TEST_CASE("Environment effects: invalid device is rejected")
{
    auto result = NebulaRenderer::create({
        .device = VK_NULL_HANDLE,
        .colorFormat = VK_FORMAT_R8G8B8A8_UNORM,
        .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
    });
    CHECK(!result.has_value());
}
