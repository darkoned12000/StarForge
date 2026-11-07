// STARFORGE sandbox: a thin host for exercising the engine libraries.
// Engine logic belongs in src/, not here.

#include "core/log.hpp"
#include "core/version.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/image.hpp"
#include "platform/window.hpp"
#include "vulkan/vulkan_context.hpp"
#include "vulkan/vulkan_device.hpp"
#include "vulkan/vulkan_renderer.hpp"
#include "vulkan/vulkan_swapchain.hpp"

// Environment effects
#include "environment/nebula_renderer.hpp"
#include "environment/dust_field.hpp"
#include "environment/particle_system.hpp"
#include "environment/starfield_renderer.hpp"
#include "environment/thruster_effect.hpp"
#include "environment/weapon_effect.hpp"
#include "environment/collision_effect.hpp"
#include "environment/sun_distortion.hpp"
#include "environment/wormhole_effect.hpp"
#include "environment/ftl_effect.hpp"
#include "environment/afterburner_effect.hpp"
#include "environment/storm_effect.hpp"
#include "environment/shield_effect.hpp"

#include <chrono>
#include <cstdlib>
#include <thread>
#include <utility>

#ifndef STARFORGE_SHADER_DIR
#define STARFORGE_SHADER_DIR "shaders"
#endif

namespace
{
constexpr const char* kTag = "STARFORGE";

struct SurfaceGuard
{
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    SurfaceGuard() = default;
    SurfaceGuard(VkInstance inst, VkSurfaceKHR surf) noexcept : instance(inst), surface(surf) {}
    ~SurfaceGuard()
    {
        if (instance != VK_NULL_HANDLE && surface != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(instance, surface, nullptr);
        }
    }
    SurfaceGuard(const SurfaceGuard&) = delete;
    SurfaceGuard& operator=(const SurfaceGuard&) = delete;
};

} // namespace

int main()
{
    using namespace starforge;

    core::logInfo(kTag, "STARFORGE Graphics Technology, version {}", core::kVersion);

    auto window = platform::Window::create({
        .title = "STARFORGE - Space Graphics Technology",
        .width = 1280,
        .height = 720,
        .resizable = true,
    });
    if (!window)
    {
        core::logError(kTag, "{}", window.error());
        return EXIT_FAILURE;
    }

    auto extensions = window->vulkanInstanceExtensions();
    if (!extensions)
    {
        core::logError(kTag, "{}", extensions.error());
        return EXIT_FAILURE;
    }

    vulkan::ContextCreateInfo contextInfo;
    contextInfo.requiredExtensions = std::move(*extensions);

    auto context = vulkan::VulkanContext::create(contextInfo);
    if (!context)
    {
        core::logError(kTag, "{}", context.error());
        return EXIT_FAILURE;
    }

    core::logInfo(kTag, "Initialization successful on {}", context->capabilities().deviceName);

    auto surfaceExpected = window->createVulkanSurface(context->instance());
    if (!surfaceExpected)
    {
        core::logError(kTag, "{}", surfaceExpected.error());
        return EXIT_FAILURE;
    }
    SurfaceGuard surface(context->instance(), *surfaceExpected);

    vulkan::VulkanDevice::CreateInfo deviceInfo;
    deviceInfo.requiredExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    deviceInfo.surface = surface.surface;

    auto device = vulkan::VulkanDevice::create(*context, deviceInfo);
    if (!device)
    {
        core::logError(kTag, "{}", device.error());
        return EXIT_FAILURE;
    }

    const auto initialSize = window->framebufferSize();
    auto swapchain = vulkan::VulkanSwapchain::create({
        .instance = context->instance(),
        .physicalDevice = context->physicalDevice(),
        .device = device->device(),
        .surface = surface.surface,
        .graphicsQueueFamily = device->graphicsQueueFamily(),
        .width = initialSize.width,
        .height = initialSize.height,
    });
    if (!swapchain)
    {
        core::logError(kTag, "{}", swapchain.error());
        return EXIT_FAILURE;
    }

    auto renderer = vulkan::FrameRenderer::create({
        .device = device->device(),
        .queueFamily = device->graphicsQueueFamily(),
        .framesInFlight = 2,
        .swapchainImageCount = static_cast<std::uint32_t>(swapchain->images().size()),
    });
    if (!renderer)
    {
        core::logError(kTag, "{}", renderer.error());
        return EXIT_FAILURE;
    }

    // Initialize environment effects (12 renderers + asteroid field).
    auto nebulaRenderer = environment::NebulaRenderer::create({
        .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .queueFamily = device->graphicsQueueFamily(),
        .colorFormat = swapchain->format(),
        .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
    });
    if (!nebulaRenderer) { core::logError(kTag, "{}", nebulaRenderer.error()); return EXIT_FAILURE; }

    auto dustFieldRenderer = environment::DustFieldRenderer::create({
        .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .queueFamily = device->graphicsQueueFamily(),
        .colorFormat = swapchain->format(),
        .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
    });
    if (!dustFieldRenderer) { core::logError(kTag, "{}", dustFieldRenderer.error()); return EXIT_FAILURE; }

    auto particleSystem = environment::ParticleSystem::create({
        .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .queueFamily = device->graphicsQueueFamily(),
        .colorFormat = swapchain->format(),
        .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
    });
    if (!particleSystem) { core::logError(kTag, "{}", particleSystem.error()); return EXIT_FAILURE; }

    auto starfieldRenderer = environment::StarfieldRenderer::create({
        .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .queueFamily = device->graphicsQueueFamily(),
        .colorFormat = swapchain->format(),
        .depthFormat = swapchain->depthFormat(),
        .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
    });
    if (!starfieldRenderer) { core::logError(kTag, "{}", starfieldRenderer.error()); return EXIT_FAILURE; }

    auto thrusterEffect = environment::ThrusterEffect::create({
        .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .queueFamily = device->graphicsQueueFamily(),
        .colorFormat = swapchain->format(),
        .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
    });
    if (!thrusterEffect) { core::logError(kTag, "{}", thrusterEffect.error()); return EXIT_FAILURE; }

    auto weaponEffect = environment::WeaponEffect::create({
        .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .queueFamily = device->graphicsQueueFamily(),
        .colorFormat = swapchain->format(),
        .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
    });
    if (!weaponEffect) { core::logError(kTag, "{}", weaponEffect.error()); return EXIT_FAILURE; }

    auto collisionEffect = environment::CollisionEffect::create({
        .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .queueFamily = device->graphicsQueueFamily(),
        .colorFormat = swapchain->format(),
        .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
    });
    if (!collisionEffect) { core::logError(kTag, "{}", collisionEffect.error()); return EXIT_FAILURE; }

    auto sunDistortionEffect = environment::SunDistortionEffect::create({
        .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .queueFamily = device->graphicsQueueFamily(),
        .colorFormat = swapchain->format(),
        .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
    });
    if (!sunDistortionEffect) { core::logError(kTag, "{}", sunDistortionEffect.error()); return EXIT_FAILURE; }

    auto wormholeEffect = environment::WormholeEffect::create({
        .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .queueFamily = device->graphicsQueueFamily(),
        .colorFormat = swapchain->format(),
        .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
    });
    if (!wormholeEffect) { core::logError(kTag, "{}", wormholeEffect.error()); return EXIT_FAILURE; }

    auto ftlEffect = environment::FtlEffect::create({
        .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .queueFamily = device->graphicsQueueFamily(),
        .colorFormat = swapchain->format(),
        .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
    });
    if (!ftlEffect) { core::logError(kTag, "{}", ftlEffect.error()); return EXIT_FAILURE; }

    auto afterburnerEffect = environment::AfterburnerEffect::create({
        .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .queueFamily = device->graphicsQueueFamily(),
        .colorFormat = swapchain->format(),
        .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
    });
    if (!afterburnerEffect) { core::logError(kTag, "{}", afterburnerEffect.error()); return EXIT_FAILURE; }

    auto stormEffect = environment::StormEffect::create({
        .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .queueFamily = device->graphicsQueueFamily(),
        .colorFormat = swapchain->format(),
        .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
    });
    if (!stormEffect) { core::logError(kTag, "{}", stormEffect.error()); return EXIT_FAILURE; }

    auto shieldEffect = environment::ShieldEffect::create({
        .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .queueFamily = device->graphicsQueueFamily(),
        .colorFormat = swapchain->format(),
        .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
    });
    if (!shieldEffect) { core::logError(kTag, "{}", shieldEffect.error()); return EXIT_FAILURE; }

    (void)nebulaRenderer; (void)dustFieldRenderer; (void)particleSystem;
    (void)thrusterEffect; (void)weaponEffect; (void)collisionEffect;
    (void)sunDistortionEffect; (void)wormholeEffect; (void)ftlEffect; (void)afterburnerEffect;
    (void)stormEffect; (void)shieldEffect;

    core::logInfo(kTag, "Close the window to exit");

    bool needRecreate = false;
    const VkClearColorValue clearColor{{0.0f, 0.0f, 0.0f, 1.0f}};
    float time = 0.0f;

    while (!window->shouldClose())
    {
        window->pollEvents();

        if (window->takeResized())
        {
            needRecreate = true;
        }

        const auto size = window->framebufferSize();
        if (window->isMinimized() || size.width == 0 || size.height == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        if (needRecreate)
        {
            core::logInfo(kTag, "Framebuffer resized to {}x{}; recreating swapchain", size.width, size.height);
            // In-flight frames may still reference the old swapchain images;
            // wait for the GPU before destroying/recreating them.
            vkDeviceWaitIdle(device->device());
            const VkFormat oldFormat = swapchain->format();
            if (auto status = swapchain->recreate(size.width, size.height); !status)
            {
                core::logError(kTag, "{}", status.error());
                return EXIT_FAILURE;
            }

            // All effect pipelines are compiled for one color format; a
            // format change (e.g. display switch) requires recreating
            // every active pipeline, not just the starfield.
            if (swapchain->format() != oldFormat)
            {
                auto newStarfieldRenderer = environment::StarfieldRenderer::create({
                    .device = device->device(),
                    .physicalDevice = context->physicalDevice(),
                    .queueFamily = device->graphicsQueueFamily(),
                    .colorFormat = swapchain->format(),
                    .depthFormat = swapchain->depthFormat(),
                    .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
                });
                if (!newStarfieldRenderer) { core::logError(kTag, "{}", newStarfieldRenderer.error()); return EXIT_FAILURE; }
                starfieldRenderer = std::move(newStarfieldRenderer);

                auto newNebula = environment::NebulaRenderer::create({
                    .device = device->device(),
                    .physicalDevice = context->physicalDevice(),
                    .queueFamily = device->graphicsQueueFamily(),
                    .colorFormat = swapchain->format(),
                    .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
                });
                if (!newNebula) { core::logError(kTag, "{}", newNebula.error()); return EXIT_FAILURE; }
                nebulaRenderer = std::move(newNebula);

                auto newDust = environment::DustFieldRenderer::create({
                    .device = device->device(),
                    .physicalDevice = context->physicalDevice(),
                    .queueFamily = device->graphicsQueueFamily(),
                    .colorFormat = swapchain->format(),
                    .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
                });
                if (!newDust) { core::logError(kTag, "{}", newDust.error()); return EXIT_FAILURE; }
                dustFieldRenderer = std::move(newDust);

                auto newParticles = environment::ParticleSystem::create({
                    .device = device->device(),
                    .physicalDevice = context->physicalDevice(),
                    .queueFamily = device->graphicsQueueFamily(),
                    .colorFormat = swapchain->format(),
                    .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
                });
                if (!newParticles) { core::logError(kTag, "{}", newParticles.error()); return EXIT_FAILURE; }
                particleSystem = std::move(newParticles);

                auto newThruster = environment::ThrusterEffect::create({
                    .device = device->device(),
                    .physicalDevice = context->physicalDevice(),
                    .queueFamily = device->graphicsQueueFamily(),
                    .colorFormat = swapchain->format(),
                    .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
                });
                if (!newThruster) { core::logError(kTag, "{}", newThruster.error()); return EXIT_FAILURE; }
                thrusterEffect = std::move(newThruster);

                auto newWeapon = environment::WeaponEffect::create({
                    .device = device->device(),
                    .physicalDevice = context->physicalDevice(),
                    .queueFamily = device->graphicsQueueFamily(),
                    .colorFormat = swapchain->format(),
                    .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
                });
                if (!newWeapon) { core::logError(kTag, "{}", newWeapon.error()); return EXIT_FAILURE; }
                weaponEffect = std::move(newWeapon);

                auto newCollision = environment::CollisionEffect::create({
                    .device = device->device(),
                    .physicalDevice = context->physicalDevice(),
                    .queueFamily = device->graphicsQueueFamily(),
                    .colorFormat = swapchain->format(),
                    .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
                });
                if (!newCollision) { core::logError(kTag, "{}", newCollision.error()); return EXIT_FAILURE; }
                collisionEffect = std::move(newCollision);

                auto newSun = environment::SunDistortionEffect::create({
                    .device = device->device(),
                    .physicalDevice = context->physicalDevice(),
                    .queueFamily = device->graphicsQueueFamily(),
                    .colorFormat = swapchain->format(),
                    .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
                });
                if (!newSun) { core::logError(kTag, "{}", newSun.error()); return EXIT_FAILURE; }
                sunDistortionEffect = std::move(newSun);

                auto newWormhole = environment::WormholeEffect::create({
                    .device = device->device(),
                    .physicalDevice = context->physicalDevice(),
                    .queueFamily = device->graphicsQueueFamily(),
                    .colorFormat = swapchain->format(),
                    .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
                });
                if (!newWormhole) { core::logError(kTag, "{}", newWormhole.error()); return EXIT_FAILURE; }
                wormholeEffect = std::move(newWormhole);

                auto newFtl = environment::FtlEffect::create({
                    .device = device->device(),
                    .physicalDevice = context->physicalDevice(),
                    .queueFamily = device->graphicsQueueFamily(),
                    .colorFormat = swapchain->format(),
                    .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
                });
                if (!newFtl) { core::logError(kTag, "{}", newFtl.error()); return EXIT_FAILURE; }
                ftlEffect = std::move(newFtl);

                auto newAfterburner = environment::AfterburnerEffect::create({
                    .device = device->device(),
                    .physicalDevice = context->physicalDevice(),
                    .queueFamily = device->graphicsQueueFamily(),
                    .colorFormat = swapchain->format(),
                    .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
                });
                if (!newAfterburner) { core::logError(kTag, "{}", newAfterburner.error()); return EXIT_FAILURE; }
                afterburnerEffect = std::move(newAfterburner);

                auto newStorm = environment::StormEffect::create({
                    .device = device->device(),
                    .physicalDevice = context->physicalDevice(),
                    .queueFamily = device->graphicsQueueFamily(),
                    .colorFormat = swapchain->format(),
                    .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
                });
                if (!newStorm) { core::logError(kTag, "{}", newStorm.error()); return EXIT_FAILURE; }
                stormEffect = std::move(newStorm);

                auto newShield = environment::ShieldEffect::create({
                    .device = device->device(),
                    .physicalDevice = context->physicalDevice(),
                    .queueFamily = device->graphicsQueueFamily(),
                    .colorFormat = swapchain->format(),
                    .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
                });
                if (!newShield) { core::logError(kTag, "{}", newShield.error()); return EXIT_FAILURE; }
                shieldEffect = std::move(newShield);
            }
            needRecreate = false;
        }

        // Render starfield into the swapchain image after the clear.
        time += 0.016f;
        environment::StarfieldRenderContext starContext;
        starContext.resolution = {static_cast<float>(size.width), static_cast<float>(size.height)};
        starContext.time = time;
        // Fixed demonstration camera (0,0,5) looking at the origin; the
        // sun billboard at worldPosition renders at screen center.
        const auto aspect =
            static_cast<float>(size.width) / std::max(static_cast<float>(size.height), 1.0f);
        starContext.viewProj = math::perspectiveVulkan(glm::radians(60.0f), aspect, 0.1f, 1000.0f) *
                               glm::lookAt(math::Vec3f{0.0f, 0.0f, 5.0f}, math::Vec3f{0.0f, 0.0f, 0.0f},
                                           math::Vec3f{0.0f, 1.0f, 0.0f});
        starContext.invViewProj = glm::inverse(starContext.viewProj);
        environment::StarfieldParams starParams;
        starParams.time = time;
        starParams.intensity = 1.0f;
        starParams.worldPosition = {0.0f, 0.0f, -50.0f};
        starParams.radius = 8.0f;
        starParams.color = {1.0f, 0.9f, 0.6f, 1.0f};

        auto frameStatus = renderer->renderFrame(
            *swapchain, device->graphicsQueue(), clearColor,
            [&](VkCommandBuffer cmd, VkExtent2D extent) {
                VkViewport viewport{};
                viewport.x = 0.0f;
                viewport.y = static_cast<float>(extent.height);
                viewport.width = static_cast<float>(extent.width);
                viewport.height = -static_cast<float>(extent.height);
                viewport.minDepth = 0.0f;
                viewport.maxDepth = 1.0f;
                vkCmdSetViewport(cmd, 0, 1, &viewport);

                VkRect2D scissor{};
                scissor.offset = {0, 0};
                scissor.extent = extent;
                vkCmdSetScissor(cmd, 0, 1, &scissor);

                starfieldRenderer->render(cmd, starContext, starParams);
            });
        if (!frameStatus)
        {
            if (frameStatus.error().find("OUT_OF_DATE") != std::string::npos ||
                frameStatus.error().find("SUBOPTIMAL") != std::string::npos)
            {
                needRecreate = true;
                continue;
            }
            core::logError(kTag, "{}", frameStatus.error());
            return EXIT_FAILURE;
        }
        if (*frameStatus == vulkan::FrameStatus::SwapchainOutOfDate)
        {
            needRecreate = true;
        }

        // No sleep here: FIFO swapchain present paces the loop (double-
        // sleeping would halve the framerate). The minimized branch above
        // already sleeps.
    }

    vkDeviceWaitIdle(device->device());

    core::logInfo(kTag, "Validation errors: {}, warnings: {}", context->validationErrorCount(), context->validationWarningCount());
    core::logInfo(kTag, "Shutting down");
    return context->validationErrorCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
