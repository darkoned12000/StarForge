#include "starforge/ui.hpp"

#include "renderer_impl.hpp"
#include "shader_dir.hpp"
#include "ui_bridge.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace starforge
{
namespace
{
namespace bridge = public_api_bridge;

[[nodiscard]] std::string resolveShaderDirectory(const std::string& requested)
{
    return public_api_detail::resolveShaderDirectory(requested, "ui_text.vert.spv");
}
} // namespace

struct UiCanvas::Impl
{
    std::unique_ptr<bridge::RawUiCanvas> backend;
    std::string shaderDirectory;
};

UiCanvas::UiCanvas(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
UiCanvas::~UiCanvas() = default;
UiCanvas::UiCanvas(UiCanvas&&) noexcept = default;
UiCanvas& UiCanvas::operator=(UiCanvas&&) noexcept = default;

std::expected<UiCanvas, std::string> UiCanvas::create(Renderer& renderer, const CreateInfo& info)
{
    if (!RendererInternalAccess::isValid(renderer))
        return std::unexpected("UiCanvas::create failed: Renderer is invalid or moved-from");
    const std::string shaderDirectory = resolveShaderDirectory(info.shaderDirectory);
    auto backend = bridge::RawUiCanvas::create({
        .instance = RendererInternalAccess::instance(renderer),
        .device = RendererInternalAccess::device(renderer),
        .physicalDevice = RendererInternalAccess::physicalDevice(renderer),
        .colorFormat = RendererInternalAccess::uiColorFormat(renderer),
        .depthFormat = RendererInternalAccess::depthFormat(renderer),
        .shaderDirectory = shaderDirectory,
    });
    if (!backend) return std::unexpected(backend.error());
    auto impl = std::make_unique<Impl>();
    impl->backend = std::move(*backend);
    impl->shaderDirectory = shaderDirectory;
    return UiCanvas(std::move(impl));
}

void UiCanvas::clear() noexcept
{
    if (impl_) impl_->backend->clear();
}

namespace
{
void copyColor(const math::Vec4f& value, float out[4]) noexcept
{
    out[0] = value.x();
    out[1] = value.y();
    out[2] = value.z();
    out[3] = value.w();
}
} // namespace

void UiCanvas::addRect(const RectDraw& draw)
{
    if (!impl_) return;
    float color[4];
    copyColor(draw.color, color);
    impl_->backend->addRect(draw.x, draw.y, draw.w, draw.h, color);
}

void UiCanvas::addPanel(const PanelDraw& draw)
{
    if (!impl_) return;
    bridge::RawUiColors colors;
    copyColor(draw.fillColor, colors.fill);
    copyColor(draw.outlineColor, colors.outline);
    copyColor(draw.accentColor, colors.accent);
    impl_->backend->addPanel(draw.x, draw.y, draw.w, draw.h, draw.outlineWidth, colors);
}

void UiCanvas::addBar(const BarDraw& draw)
{
    if (!impl_) return;
    float fill[4], track[4];
    copyColor(draw.fillColor, fill);
    copyColor(draw.trackColor, track);
    impl_->backend->addBar(draw.x, draw.y, draw.w, draw.h, draw.value, fill, track);
}

void UiCanvas::addText(const TextDraw& draw)
{
    if (!impl_) return;
    bridge::RawUiTextDraw raw;
    raw.x = draw.x;
    raw.y = draw.y;
    raw.scale = draw.scale;
    raw.color[0] = draw.color.x();
    raw.color[1] = draw.color.y();
    raw.color[2] = draw.color.z();
    raw.color[3] = draw.color.w();
    raw.text = draw.text;
    impl_->backend->addText(raw);
}

void UiCanvas::record(FrameContext& frameContext)
{
    if (!impl_) return;
    const Size2D extent = frameContext.extent();
    impl_->backend->render(RendererInternalAccess::commandBuffer(frameContext),
                           static_cast<float>(extent.width), static_cast<float>(extent.height));
}

std::size_t UiCanvas::vertexCount() const noexcept
{
    return impl_ ? impl_->backend->vertexCount() : 0u;
}

std::expected<void, std::string> UiCanvas::recreate(Renderer& renderer)
{
    if (!impl_ || !impl_->backend) return std::unexpected("UiCanvas is moved-from");
    if (!RendererInternalAccess::isValid(renderer))
        return std::unexpected("UiCanvas::recreate failed: Renderer is invalid or moved-from");
    renderer.waitIdle();
    return impl_->backend->recreate({
        .instance = RendererInternalAccess::instance(renderer),
        .device = RendererInternalAccess::device(renderer),
        .physicalDevice = RendererInternalAccess::physicalDevice(renderer),
        .colorFormat = RendererInternalAccess::uiColorFormat(renderer),
        .depthFormat = RendererInternalAccess::depthFormat(renderer),
        .shaderDirectory = impl_->shaderDirectory,
    });
}

} // namespace starforge
