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
};

UiCanvas::UiCanvas(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
UiCanvas::~UiCanvas() = default;
UiCanvas::UiCanvas(UiCanvas&&) noexcept = default;
UiCanvas& UiCanvas::operator=(UiCanvas&&) noexcept = default;

std::expected<UiCanvas, std::string> UiCanvas::create(Renderer& renderer, const CreateInfo& info)
{
    if (!RendererInternalAccess::isValid(renderer))
        return std::unexpected("UiCanvas::create failed: Renderer is invalid or moved-from");
    auto backend = bridge::RawUiCanvas::create({
        .instance = RendererInternalAccess::instance(renderer),
        .device = RendererInternalAccess::device(renderer),
        .physicalDevice = RendererInternalAccess::physicalDevice(renderer),
        .colorFormat = RendererInternalAccess::colorFormat(renderer),
        .depthFormat = RendererInternalAccess::depthFormat(renderer),
        .shaderDirectory = resolveShaderDirectory(info.shaderDirectory),
    });
    if (!backend) return std::unexpected(backend.error());
    auto impl = std::make_unique<Impl>();
    impl->backend = std::move(*backend);
    return UiCanvas(std::move(impl));
}

void UiCanvas::clear() noexcept
{
    if (impl_) impl_->backend->clear();
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

} // namespace starforge
