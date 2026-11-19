#include "starforge/hud.hpp"

#include "hud_bridge.hpp"
#include "renderer_impl.hpp"
#include "shader_dir.hpp"

#include <algorithm>
#include <utility>

namespace starforge
{

namespace
{
namespace bridge = public_api_bridge;

bridge::RawVec4f toRaw(const math::Vec4f& value) noexcept
{
    return bridge::RawVec4f{value.x(), value.y(), value.z(), value.w()};
}

std::string resolveShaderDirectory(const std::string& requested)
{
    return public_api_detail::resolveShaderDirectory(requested, "hud.frag.spv");
}
} // namespace

struct Hud::Impl
{
    std::unique_ptr<bridge::RawHud> backend;
    std::string shaderDirectory;
};

Hud::Hud(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Hud::~Hud() = default;
Hud::Hud(Hud&&) noexcept = default;
Hud& Hud::operator=(Hud&&) noexcept = default;

std::expected<Hud, std::string> Hud::create(Renderer& renderer, const CreateInfo& info)
{
    if (!RendererInternalAccess::isValid(renderer))
        return std::unexpected("Hud::create failed: Renderer is invalid or moved-from");

    const std::string shaderDirectory = resolveShaderDirectory(info.shaderDirectory);
    auto backend = bridge::RawHud::create({
        .instance = RendererInternalAccess::instance(renderer),
        .device = RendererInternalAccess::device(renderer),
        .physicalDevice = RendererInternalAccess::physicalDevice(renderer),
        .colorFormat = RendererInternalAccess::colorFormat(renderer),
        .depthFormat = RendererInternalAccess::depthFormat(renderer),
        .shaderDirectory = shaderDirectory,
    });
    if (!backend) return std::unexpected(backend.error());
    auto impl = std::make_unique<Impl>();
    impl->backend = std::move(*backend);
    impl->shaderDirectory = shaderDirectory;
    return Hud(std::move(impl));
}

void Hud::record(FrameContext& frameContext, const Params& params)
{
    if (!impl_ || !impl_->backend) return;
    bridge::RawHudParams raw;
    raw.panelX = params.panelX;
    raw.panelY = params.panelY;
    raw.panelWidth = params.panelWidth;
    raw.panelHeight = params.panelHeight;
    raw.portraitX = params.portraitX;
    raw.portraitY = params.portraitY;
    raw.portraitWidth = params.portraitWidth;
    raw.portraitHeight = params.portraitHeight;
    const auto panel = toRaw(params.panelColor);
    raw.panelColor[0] = panel.x; raw.panelColor[1] = panel.y;
    raw.panelColor[2] = panel.z; raw.panelColor[3] = panel.w;
    const auto outline = toRaw(params.outlineColor);
    raw.outlineColor[0] = outline.x; raw.outlineColor[1] = outline.y;
    raw.outlineColor[2] = outline.z; raw.outlineColor[3] = outline.w;
    const auto accent = toRaw(params.accentColor);
    raw.accentColor[0] = accent.x; raw.accentColor[1] = accent.y;
    raw.accentColor[2] = accent.z; raw.accentColor[3] = accent.w;
    std::copy_n(params.barValues, 3, raw.barValues);

    const Size2D extent = frameContext.extent();
    impl_->backend->render(RendererInternalAccess::commandBuffer(frameContext),
                           static_cast<float>(extent.width), static_cast<float>(extent.height), raw);
}

std::expected<void, std::string> Hud::recreate(Renderer& renderer)
{
    if (!impl_ || !impl_->backend) return std::unexpected("Hud is moved-from");
    renderer.waitIdle();
    return impl_->backend->recreate({
        .instance = RendererInternalAccess::instance(renderer),
        .device = RendererInternalAccess::device(renderer),
        .physicalDevice = RendererInternalAccess::physicalDevice(renderer),
        .colorFormat = RendererInternalAccess::colorFormat(renderer),
        .depthFormat = RendererInternalAccess::depthFormat(renderer),
        .shaderDirectory = impl_->shaderDirectory,
    });
}

} // namespace starforge
