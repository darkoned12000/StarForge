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

#include <chrono>
#include <cstdlib>
#include <thread>
#include <utility>

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

    core::logInfo(kTag, "Close the window to exit");

    bool needRecreate = false;
    const VkClearColorValue clearColor{{0.02f, 0.03f, 0.08f, 1.0f}};

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
            if (auto status = swapchain->recreate(size.width, size.height); !status)
            {
                core::logError(kTag, "{}", status.error());
                return EXIT_FAILURE;
            }
            needRecreate = false;
        }

        auto frameStatus = renderer->renderClearedFrame(*swapchain, device->graphicsQueue(), clearColor);
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
