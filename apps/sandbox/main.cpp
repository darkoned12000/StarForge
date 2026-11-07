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

    // Initialize environment effects. Only the starfield renders today;
    // the rest are validated handles awaiting their pipelines (TODO M8).
    auto nebulaRenderer = environment::NebulaRenderer::create({
        .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .queueFamily = device->graphicsQueueFamily()
    });
    if (!nebulaRenderer) { core::logError(kTag, "{}", nebulaRenderer.error()); return EXIT_FAILURE; }

    auto dustFieldRenderer = environment::DustFieldRenderer::create({
        .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .queueFamily = device->graphicsQueueFamily()
    });
    if (!dustFieldRenderer) { core::logError(kTag, "{}", dustFieldRenderer.error()); return EXIT_FAILURE; }

    auto particleSystem = environment::ParticleSystem::create({
        .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .queueFamily = device->graphicsQueueFamily()
    });
    if (!particleSystem) { core::logError(kTag, "{}", particleSystem.error()); return EXIT_FAILURE; }

    auto starfieldRenderer = environment::StarfieldRenderer::create({
        .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .queueFamily = device->graphicsQueueFamily(),
        .colorFormat = swapchain->format(),
        .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
    });
    if (!starfieldRenderer) { core::logError(kTag, "{}", starfieldRenderer.error()); return EXIT_FAILURE; }

    auto thrusterEffect = environment::ThrusterEffect::create({
        .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .queueFamily = device->graphicsQueueFamily()
    });
    if (!thrusterEffect) { core::logError(kTag, "{}", thrusterEffect.error()); return EXIT_FAILURE; }

    auto weaponEffect = environment::WeaponEffect::create({
        .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .queueFamily = device->graphicsQueueFamily()
    });
    if (!weaponEffect) { core::logError(kTag, "{}", weaponEffect.error()); return EXIT_FAILURE; }

    auto collisionEffect = environment::CollisionEffect::create({
        .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .queueFamily = device->graphicsQueueFamily()
    });
    if (!collisionEffect) { core::logError(kTag, "{}", collisionEffect.error()); return EXIT_FAILURE; }

    auto sunDistortionEffect = environment::SunDistortionEffect::create({
        .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .queueFamily = device->graphicsQueueFamily()
    });
    if (!sunDistortionEffect) { core::logError(kTag, "{}", sunDistortionEffect.error()); return EXIT_FAILURE; }

    auto wormholeEffect = environment::WormholeEffect::create({
        .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .queueFamily = device->graphicsQueueFamily()
    });
    if (!wormholeEffect) { core::logError(kTag, "{}", wormholeEffect.error()); return EXIT_FAILURE; }

    auto ftlEffect = environment::FtlEffect::create({
        .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .queueFamily = device->graphicsQueueFamily()
    });
    if (!ftlEffect) { core::logError(kTag, "{}", ftlEffect.error()); return EXIT_FAILURE; }

    auto afterburnerEffect = environment::AfterburnerEffect::create({
        .device = device->device(),
        .physicalDevice = context->physicalDevice(),
        .queueFamily = device->graphicsQueueFamily()
    });
    if (!afterburnerEffect) { core::logError(kTag, "{}", afterburnerEffect.error()); return EXIT_FAILURE; }

    (void)nebulaRenderer; (void)dustFieldRenderer; (void)particleSystem;
    (void)thrusterEffect; (void)weaponEffect; (void)collisionEffect;
    (void)sunDistortionEffect; (void)wormholeEffect; (void)ftlEffect; (void)afterburnerEffect;

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

            // The star pipeline is compiled for one color format; a format
            // change (e.g. display switch) requires recreating it.
            if (swapchain->format() != oldFormat)
            {
                auto newStarfieldRenderer = environment::StarfieldRenderer::create({
                    .device = device->device(),
                    .physicalDevice = context->physicalDevice(),
                    .queueFamily = device->graphicsQueueFamily(),
                    .colorFormat = swapchain->format(),
                    .shaderDirectory = std::string(STARFORGE_SHADER_DIR),
                });
                if (!newStarfieldRenderer)
                {
                    core::logError(kTag, "{}", newStarfieldRenderer.error());
                    return EXIT_FAILURE;
                }
                starfieldRenderer = std::move(newStarfieldRenderer);
            }
            needRecreate = false;
        }

        // Render starfield into the swapchain image after the clear.
        time += 0.016f;
        environment::StarfieldRenderContext starContext;
        starContext.resolution = {static_cast<float>(size.width), static_cast<float>(size.height)};
        starContext.time = time;
        environment::StarfieldParams starParams;
        starParams.time = time;
        starParams.intensity = 1.0f;

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

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    vkDeviceWaitIdle(device->device());

    core::logInfo(kTag, "Validation errors: {}, warnings: {}", context->validationErrorCount(), context->validationWarningCount());
    core::logInfo(kTag, "Shutting down");
    return context->validationErrorCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
