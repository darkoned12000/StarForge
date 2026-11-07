#include "starforge/renderer.hpp"
#include "renderer_impl.hpp"

#include "application_impl.hpp"

#include <utility>

namespace starforge
{

Renderer::Renderer(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl))
{
}

Renderer::~Renderer() = default;

Renderer::Renderer(Renderer&& other) noexcept : impl_(std::move(other.impl_))
{
}

Renderer& Renderer::operator=(Renderer&& other) noexcept
{
    if (this != &other)
    {
        if (impl_)
        {
            impl_->waitIdle();
        }
        impl_ = std::move(other.impl_);
    }
    return *this;
}

void Renderer::waitIdle() const noexcept
{
    if (impl_)
    {
        impl_->waitIdle();
    }
}

std::expected<Renderer, std::string> Renderer::create(Application& app, const CreateInfo& info)
{
    if (info.framesInFlight == 0)
    {
        return std::unexpected("Renderer framesInFlight must be at least 1");
    }

    auto impl = std::make_unique<Impl>();
    impl->app = &app;
    auto& window = ApplicationInternalAccess::getWindow(app);

    auto extensions = window.vulkanInstanceExtensions();
    if (!extensions)
    {
        return std::unexpected(extensions.error());
    }

    vulkan::ContextCreateInfo contextInfo;
    contextInfo.requiredExtensions = std::move(*extensions);

    auto context = vulkan::VulkanContext::create(contextInfo);
    if (!context)
    {
        return std::unexpected(context.error());
    }
    impl->context = std::make_unique<vulkan::VulkanContext>(std::move(*context));

    auto surface = window.createVulkanSurface(impl->context->instance());
    if (!surface)
    {
        return std::unexpected(surface.error());
    }
    impl->surface = *surface;

    vulkan::VulkanDevice::CreateInfo deviceInfo;
    deviceInfo.requiredExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    deviceInfo.surface = impl->surface;

    auto device = vulkan::VulkanDevice::create(*impl->context, deviceInfo);
    if (!device)
    {
        return std::unexpected(device.error());
    }
    impl->device = std::make_unique<vulkan::VulkanDevice>(std::move(*device));

    const auto size = app.framebufferSize();
    if (size.width == 0 || size.height == 0)
    {
        return std::unexpected("Renderer requires a non-empty framebuffer at creation");
    }

    auto swapchain = vulkan::VulkanSwapchain::create({
        .instance = impl->context->instance(),
        .physicalDevice = impl->context->physicalDevice(),
        .device = impl->device->device(),
        .surface = impl->surface,
        .graphicsQueueFamily = impl->device->graphicsQueueFamily(),
        .width = size.width,
        .height = size.height,
    });
    if (!swapchain)
    {
        return std::unexpected(swapchain.error());
    }
    impl->swapchain = std::make_unique<vulkan::VulkanSwapchain>(std::move(*swapchain));

    auto frameRenderer = vulkan::FrameRenderer::create({
        .device = impl->device->device(),
        .queueFamily = impl->device->graphicsQueueFamily(),
        .framesInFlight = info.framesInFlight,
        .swapchainImageCount = static_cast<std::uint32_t>(impl->swapchain->images().size()),
    });
    if (!frameRenderer)
    {
        return std::unexpected(frameRenderer.error());
    }
    impl->frameRenderer = std::make_unique<vulkan::FrameRenderer>(std::move(*frameRenderer));

    return Renderer(std::move(impl));
}

namespace
{

[[nodiscard]] std::expected<Renderer::FrameResult, std::string>
mapFrameStatus(std::expected<vulkan::FrameStatus, std::string>&& status)
{
    if (!status)
    {
        if (status.error().find("OUT_OF_DATE") != std::string::npos ||
            status.error().find("SUBOPTIMAL") != std::string::npos)
        {
            return Renderer::FrameResult::SwapchainOutOfDate;
        }
        return std::unexpected(status.error());
    }
    return *status == vulkan::FrameStatus::SwapchainOutOfDate ? Renderer::FrameResult::SwapchainOutOfDate
                                                                : Renderer::FrameResult::Presented;
}

} // namespace

Size2D FrameContext::extent() const noexcept
{
    return extent_;
}

std::expected<Renderer::FrameResult, std::string> Renderer::renderClearedFrame(const ClearColor& clearColor)
{
    if (!impl_)
    {
        return std::unexpected("Renderer is moved-from");
    }

    const VkClearColorValue clear{{clearColor.red, clearColor.green, clearColor.blue, clearColor.alpha}};
    return mapFrameStatus(
        impl_->frameRenderer->renderClearedFrame(*impl_->swapchain, impl_->device->graphicsQueue(), clear));
}

std::expected<Renderer::FrameResult, std::string> Renderer::renderFrame(const ClearColor& clearColor, const RecordFn& record)
{
    if (!impl_)
    {
        return std::unexpected("Renderer is moved-from");
    }
    if (!record)
    {
        return std::unexpected("Renderer::renderFrame requires a record callback");
    }

    const VkClearColorValue clear{{clearColor.red, clearColor.green, clearColor.blue, clearColor.alpha}};
    return mapFrameStatus(impl_->frameRenderer->renderFrame(
        *impl_->swapchain, impl_->device->graphicsQueue(), clear,
        [&record](VkCommandBuffer cmd, VkExtent2D extent) {
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

            FrameContext frameContext;
            frameContext.commandBuffer_ = cmd;
            frameContext.extent_ = Size2D{.width = extent.width, .height = extent.height};
            record(frameContext);
        }));
}

std::expected<Renderer::RecreateResult, std::string> Renderer::recreate()
{
    if (!impl_)
    {
        return std::unexpected("Renderer is moved-from");
    }

    const auto size = impl_->app->framebufferSize();
    if (size.width == 0 || size.height == 0 || impl_->app->isMinimized())
    {
        return std::unexpected("Cannot recreate swapchain while the framebuffer is minimized or empty");
    }

    const VkFormat oldFormat = impl_->swapchain->format();
    vkDeviceWaitIdle(impl_->device->device());
    if (auto status = impl_->swapchain->recreate(size.width, size.height); !status)
    {
        return std::unexpected(status.error());
    }
    if (auto status = impl_->frameRenderer->ensureSwapchainImageCount(
            static_cast<std::uint32_t>(impl_->swapchain->images().size()));
        !status)
    {
        return std::unexpected(status.error());
    }
    return impl_->swapchain->format() != oldFormat ? RecreateResult::FormatChanged : RecreateResult::Resized;
}

Size2D Renderer::extent() const noexcept
{
    if (!impl_ || !impl_->swapchain)
    {
        return {};
    }
    const auto extent = impl_->swapchain->extent();
    return Size2D{.width = extent.width, .height = extent.height};
}

} // namespace starforge
