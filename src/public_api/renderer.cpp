#include "starforge/renderer.hpp"
#include "renderer_impl.hpp"

#include "application_impl.hpp"
#include "shader_dir.hpp"
#include "core/log.hpp"
#include "vulkan/vulkan_retirement.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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

    impl->hdrEnabled = RendererInternalAccess::hdrRequested();
    if (auto hdr = RendererInternalAccess::createHdrResources(*impl); !hdr)
    {
        // An HDR setup failure must not break rendering: fall back to the
        // direct swapchain path so the game still starts.
        core::logWarning("RENDERER", "HDR setup failed, using direct path: {}", hdr.error());
        impl->hdrEnabled = false;
        RendererInternalAccess::destroyHdrResources(*impl);
    }

    return Renderer(std::move(impl));
}

// STARFORGE_HDR=0 restores the original direct-to-swapchain path, useful for
// bisecting visual or performance regressions.
bool RendererInternalAccess::hdrRequested() noexcept
{
    const char* value = std::getenv("STARFORGE_HDR");
    return value == nullptr || std::strcmp(value, "0") != 0;
}

[[nodiscard]] std::string resolveTonemapShaderDirectory()
{
    return public_api_detail::resolveShaderDirectory("", "hdr_resolve.vert.spv");
}

void RendererInternalAccess::destroyHdrResources(Renderer::Impl& impl) noexcept
{
    impl.destroyHdrResources();
}

std::expected<void, std::string> RendererInternalAccess::createHdrResources(Renderer::Impl& impl)
{
    if (!impl.hdrEnabled)
        return {};

    const auto format = impl.swapchain->format();
    if (format == VK_FORMAT_R16G16B16A16_SFLOAT)
    {
        // No offscreen pass, so there is no tonemap. Leave hdrEnabled false
        // or facades would receive the linear HDR specialization while
        // renderFrame skips tonemapping - un-encoded output to the screen.
        impl.hdrEnabled = false;
        return {};
    }

    if (!impl.frameRenderer)
        return std::unexpected("HDR resources require an initialized FrameRenderer");

    impl.hdrExtent = impl.swapchain->extent();
    auto allocator = gpu_resources::Allocator::create({
        .instance = impl.context->instance(),
        .physicalDevice = impl.context->physicalDevice(),
        .device = impl.device->device(),
    });
    if (!allocator) return std::unexpected(allocator.error());
    impl.allocator = std::make_unique<gpu_resources::Allocator>(std::move(*allocator));

    const std::uint32_t framesInFlight = impl.frameRenderer->framesInFlight();
    impl.hdrFrames.clear();
    impl.hdrFrames.reserve(framesInFlight);
    impl.hdrColorImages.assign(framesInFlight, VK_NULL_HANDLE);
    impl.hdrColorViews.assign(framesInFlight, VK_NULL_HANDLE);
    impl.hdrDepthImages.assign(framesInFlight, VK_NULL_HANDLE);
    impl.hdrDepthViews.assign(framesInFlight, VK_NULL_HANDLE);

    const std::uint32_t bw = std::max(1u, impl.hdrExtent.width / 2);
    const std::uint32_t bh = std::max(1u, impl.hdrExtent.height / 2);

    for (std::uint32_t i = 0; i < framesInFlight; ++i)
    {
        Renderer::Impl::HdrFrameResources frame;

        auto color = gpu_resources::Image::create(*impl.allocator, {
            .width = impl.hdrExtent.width,
            .height = impl.hdrExtent.height,
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .usage = gpu_resources::ImageUsageFlags::ColorAttachment |
                     gpu_resources::ImageUsageFlags::Sampled,
            .name = "hdr-scene-color",
        });
        if (!color) return std::unexpected(color.error());
        auto colorView = color->createImageView(impl.device->device());
        if (!colorView) return std::unexpected(colorView.error());

        auto depth = gpu_resources::Image::create(*impl.allocator, {
            .width = impl.hdrExtent.width,
            .height = impl.hdrExtent.height,
            .format = impl.swapchain->depthFormat(),
            .usage = gpu_resources::ImageUsageFlags::DepthAttachment,
            .name = "hdr-scene-depth",
        });
        if (!depth) return std::unexpected(depth.error());
        auto depthView = depth->createImageView(impl.device->device());
        if (!depthView) return std::unexpected(depthView.error());

        auto makeBloomTarget = [&](const char* name)
            -> std::expected<std::pair<gpu_resources::Image, gpu_resources::Image::ImageView>, std::string> {
            auto img = gpu_resources::Image::create(*impl.allocator, {
                .width = bw, .height = bh,
                .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                .usage = gpu_resources::ImageUsageFlags::ColorAttachment |
                         gpu_resources::ImageUsageFlags::Sampled,
                .name = name,
            });
            if (!img) return std::unexpected(img.error());
            auto view = img->createImageView(impl.device->device());
            if (!view) return std::unexpected(view.error());
            return std::expected<std::pair<gpu_resources::Image, gpu_resources::Image::ImageView>, std::string>(
                std::make_pair(std::move(*img), std::move(*view)));
        };

        auto bloomA = makeBloomTarget("bloom-a");
        if (!bloomA) return std::unexpected(bloomA.error());
        auto bloomB = makeBloomTarget("bloom-b");
        if (!bloomB) return std::unexpected(bloomB.error());

        impl.hdrColorImages[i] = color->handle();
        impl.hdrColorViews[i] = colorView->handle();
        impl.hdrDepthImages[i] = depth->handle();
        impl.hdrDepthViews[i] = depthView->handle();

        frame.color = std::make_unique<gpu_resources::Image>(std::move(*color));
        frame.colorView = std::make_unique<gpu_resources::Image::ImageView>(std::move(*colorView));
        frame.depth = std::make_unique<gpu_resources::Image>(std::move(*depth));
        frame.depthView = std::make_unique<gpu_resources::Image::ImageView>(std::move(*depthView));
        frame.bloomA = std::make_unique<gpu_resources::Image>(std::move(bloomA->first));
        frame.bloomAView = std::make_unique<gpu_resources::Image::ImageView>(std::move(bloomA->second));
        frame.bloomB = std::make_unique<gpu_resources::Image>(std::move(bloomB->first));
        frame.bloomBView = std::make_unique<gpu_resources::Image::ImageView>(std::move(bloomB->second));
        impl.hdrFrames.push_back(std::move(frame));
    }

    auto tonemap = environment::internal::TextureEffect::create({
        .device = impl.device->device(),
        .colorFormat = format,
        // Pass 2 keeps the swapchain depth attached (LOAD) so the UI
        // pipelines remain dynamic-rendering compatible. The tonemap declares
        // the same depth format with test/write disabled.
        .depthFormat = impl.swapchain->depthFormat(),
        .shaderDirectory = resolveTonemapShaderDirectory(),
        .shaderBaseName = "hdr_resolve",
        .pushConstantSize = 32,
    });
    if (!tonemap) return std::unexpected(tonemap.error());

    const std::string dir = resolveTonemapShaderDirectory();
    auto bright = environment::internal::TextureEffect::create({
        .device = impl.device->device(),
        .colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT,
        .shaderDirectory = dir, .shaderBaseName = "hdr_bright", .pushConstantSize = 16,
    });
    if (!bright) return std::unexpected(bright.error());
    auto blur = environment::internal::TextureEffect::create({
        .device = impl.device->device(),
        .colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT,
        .shaderDirectory = dir, .shaderBaseName = "hdr_blur", .pushConstantSize = 16,
    });
    if (!blur) return std::unexpected(blur.error());

    impl.tonemap = std::make_unique<environment::internal::TextureEffect>(std::move(*tonemap));
    impl.brightPass = std::make_unique<environment::internal::TextureEffect>(std::move(*bright));
    impl.blurPass = std::make_unique<environment::internal::TextureEffect>(std::move(*blur));
    return {};
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

    // Route through renderFrame so HDR-enabled renderers clear/tonemap the
    // offscreen scene consistently with normal frames.
    return renderFrame(clearColor, [](FrameContext&) {});
}

std::expected<Renderer::FrameResult, std::string>
Renderer::renderFrame(const ClearColor& clearColor, const RecordFn& record, const RecordFn& uiRecord)
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

    vulkan::FrameRenderer::OffscreenSceneTarget hdrTarget;
    const bool hdrActive = impl_->hdrEnabled && !impl_->hdrFrames.empty() && impl_->tonemap;
    if (hdrActive)
    {
        hdrTarget.colorImages = impl_->hdrColorImages.data();
        hdrTarget.colorViews = impl_->hdrColorViews.data();
        hdrTarget.depthImages = impl_->hdrDepthImages.empty() ? nullptr : impl_->hdrDepthImages.data();
        hdrTarget.depthViews = impl_->hdrDepthViews.empty() ? nullptr : impl_->hdrDepthViews.data();
        hdrTarget.frameCount = static_cast<std::uint32_t>(impl_->hdrFrames.size());
        hdrTarget.depthFormat = impl_->swapchain->depthFormat();
        hdrTarget.extent = impl_->hdrExtent;

        // Bloom chain: bright extract at half resolution, then separable
        // blur H/V. Per-frame bloom targets avoid hazards when multiple
        // frames are in flight.
        hdrTarget.bloom = [impl = impl_.get()](VkCommandBuffer cmd, VkExtent2D /*extent*/,
                                              VkImageView sceneColor, std::uint32_t frameIndex) {
            if (!impl->brightPass || !impl->blurPass || frameIndex >= impl->hdrFrames.size())
                return;
            auto& frame = impl->hdrFrames[frameIndex];
            if (!frame.bloomA || !frame.bloomAView || !frame.bloomB || !frame.bloomBView)
                return;

            const auto transition = [&](VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                                        VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                        VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
                VkImageMemoryBarrier2 b{};
                b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                b.srcStageMask = srcStage; b.srcAccessMask = srcAccess;
                b.dstStageMask = dstStage; b.dstAccessMask = dstAccess;
                b.oldLayout = oldLayout; b.newLayout = newLayout;
                b.image = image;
                b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                b.subresourceRange.levelCount = 1;
                b.subresourceRange.layerCount = 1;
                VkDependencyInfo dep{};
                dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                dep.imageMemoryBarrierCount = 1;
                dep.pImageMemoryBarriers = &b;
                vkCmdPipelineBarrier2(cmd, &dep);
            };
            const auto setViewport = [&](std::uint32_t w, std::uint32_t h) {
                VkViewport vp{0.0f, static_cast<float>(h), static_cast<float>(w),
                              -static_cast<float>(h), 0.0f, 1.0f};
                VkRect2D sc{{0, 0}, {w, h}};
                vkCmdSetViewport(cmd, 0, 1, &vp);
                vkCmdSetScissor(cmd, 0, 1, &sc);
            };
            const auto beginPass = [&](VkImageView view, std::uint32_t w, std::uint32_t h) {
                VkRenderingAttachmentInfo a{};
                a.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                a.imageView = view;
                a.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                VkRenderingInfo info{};
                info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                info.renderArea.extent = {w, h};
                info.layerCount = 1;
                info.colorAttachmentCount = 1;
                info.pColorAttachments = &a;
                vkCmdBeginRendering(cmd, &info);
            };

            const VkExtent2D half{frame.bloomA->width(), frame.bloomA->height()};

            transition(frame.bloomA->handle(), VK_IMAGE_LAYOUT_UNDEFINED,
                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                       VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
            setViewport(half.width, half.height);
            beginPass(frame.bloomAView->handle(), half.width, half.height);
            const float brightPush[4] = {1.0f, 0.0f, 0.0f, 0.0f}; // threshold 1.0
            impl->brightPass->render(cmd, sceneColor, brightPush, sizeof(brightPush));
            vkCmdEndRendering(cmd);

            transition(frame.bloomA->handle(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            transition(frame.bloomB->handle(), VK_IMAGE_LAYOUT_UNDEFINED,
                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                       VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
            setViewport(half.width, half.height);
            beginPass(frame.bloomBView->handle(), half.width, half.height);
            const float texel[2] = {1.0f / static_cast<float>(half.width),
                                    1.0f / static_cast<float>(half.height)};
            const float blurH[4] = {texel[0], texel[1], 0.0f, 0.0f};
            impl->blurPass->render(cmd, frame.bloomAView->handle(), blurH, sizeof(blurH));
            vkCmdEndRendering(cmd);

            transition(frame.bloomB->handle(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            transition(frame.bloomA->handle(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                       VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
            setViewport(half.width, half.height);
            beginPass(frame.bloomAView->handle(), half.width, half.height);
            const float blurV[4] = {texel[0], texel[1], 1.0f, 0.0f};
            impl->blurPass->render(cmd, frame.bloomBView->handle(), blurV, sizeof(blurV));
            vkCmdEndRendering(cmd);

            transition(frame.bloomA->handle(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        };

        hdrTarget.resolve = [impl = impl_.get(), &uiRecord](VkCommandBuffer cmd, VkExtent2D extent,
                                                VkImageView sceneColor, std::uint32_t frameIndex) {
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

            struct TonemapPush
            {
                float exposure;
                float bloomStrength;
                float vignette;
                std::uint32_t mode;
                std::uint32_t frame;
                float pad0;
                float pad1;
                float pad2;
            } push{};
            push.exposure = impl->hdrExposure;
            push.bloomStrength = 0.6f;
            push.vignette = 0.15f;
            push.mode = 1u; // PBR Neutral: preserves stylized palette
            push.frame = impl->hdrFrameCounter++ & 63u;
            const VkImageView bloomView = (frameIndex < impl->hdrFrames.size() && impl->hdrFrames[frameIndex].bloomAView)
                                              ? impl->hdrFrames[frameIndex].bloomAView->handle()
                                              : VK_NULL_HANDLE;
            impl->tonemap->render(cmd, sceneColor, &push, sizeof(push), bloomView);

            if (uiRecord)
            {
                FrameContext uiContext;
                uiContext.commandBuffer_ = cmd;
                uiContext.extent_ = Size2D{.width = extent.width, .height = extent.height};
                uiRecord(uiContext);
            }
        };
    }

    return mapFrameStatus(impl_->frameRenderer->renderFrame(
        *impl_->swapchain, impl_->device->graphicsQueue(), clear,
        [&record, &uiRecord, hdrActive](VkCommandBuffer cmd, VkExtent2D extent) {
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
            // Legacy path: UI shares the scene pass; with HDR it runs after
            // tonemapping in the resolve callback instead.
            if (!hdrActive && uiRecord) uiRecord(frameContext);
        },
        hdrTarget.colorImages != nullptr ? &hdrTarget : nullptr));
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
    if (auto status = impl_->frameRenderer->waitForAllFrames(); !status)
    {
        return std::unexpected(status.error());
    }
    // All frame fences are signaled; retire old swapchain resources through the
    // retirement queue to use the deferred-recreate path.
    vulkan::GpuRetirementQueue retirement;
    const VkFence retireFence = impl_->frameRenderer->currentFence();
    if (retireFence != VK_NULL_HANDLE)
    {
        if (auto status = impl_->swapchain->recreate(size.width, size.height, retirement, retireFence); !status)
        {
            return std::unexpected(status.error());
        }
        (void)retirement.collect(impl_->device->device());
    }
    else
    {
        if (auto status = impl_->swapchain->recreate(size.width, size.height); !status)
        {
            return std::unexpected(status.error());
        }
    }
    if (auto status = impl_->frameRenderer->ensureSwapchainImageCount(
            static_cast<std::uint32_t>(impl_->swapchain->images().size()));
        !status)
    {
        return std::unexpected(status.error());
    }

    bool hdrModeChanged = false;
    const bool requestedHdr = RendererInternalAccess::hdrRequested();
    if (requestedHdr != impl_->hdrEnabled)
    {
        // Env override changed, or the swapchain flipped between HDR and SDR
        // (e.g. window moved to another monitor). Re-evaluate rather than
        // keeping the old mode for the rest of the session.
        impl_->hdrEnabled = requestedHdr;
        hdrModeChanged = true;
    }
    if (impl_->hdrEnabled)
    {
        // The offscreen targets are sized to the swapchain; rebuild them on
        // every resize. Old views/images must go before the allocator.
        RendererInternalAccess::destroyHdrResources(*impl_);
        if (auto hdr = RendererInternalAccess::createHdrResources(*impl_); !hdr)
        {
            core::logWarning("RENDERER", "HDR recreate failed, using direct path: {}", hdr.error());
            impl_->hdrEnabled = false;
            RendererInternalAccess::destroyHdrResources(*impl_);
            hdrModeChanged = true; // scene format/specialization changed
        }
    }

    const bool formatChanged = hdrModeChanged || impl_->swapchain->format() != oldFormat;
    return formatChanged ? RecreateResult::FormatChanged : RecreateResult::Resized;
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
