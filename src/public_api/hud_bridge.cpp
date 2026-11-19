#include "hud_bridge.hpp"

#include "environment/hud_renderer.hpp"

#include <algorithm>
#include <utility>

namespace starforge::public_api_bridge
{

struct RawHud::Impl
{
    environment::HudRenderer renderer;
};

RawHud::~RawHud() = default;
RawHud::RawHud(RawHud&&) noexcept = default;
RawHud& RawHud::operator=(RawHud&&) noexcept = default;

std::expected<std::unique_ptr<RawHud>, std::string> RawHud::create(const CreateInfo& info)
{
    auto renderer = environment::HudRenderer::create({
        .instance = info.instance, .device = info.device,
        .physicalDevice = info.physicalDevice, .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .shaderDirectory = info.shaderDirectory});
    if (!renderer) return std::unexpected(renderer.error());
    auto result = std::unique_ptr<RawHud>(new RawHud());
    result->impl_ = std::make_unique<Impl>(Impl{std::move(*renderer)});
    return result;
}

void RawHud::render(VkCommandBuffer cmd, float width, float height, const RawHudParams& params)
{
    if (!impl_) return;
    environment::HudParams internal;
    internal.panelX = params.panelX;
    internal.panelY = params.panelY;
    internal.panelWidth = params.panelWidth;
    internal.panelHeight = params.panelHeight;
    internal.portraitX = params.portraitX;
    internal.portraitY = params.portraitY;
    internal.portraitWidth = params.portraitWidth;
    internal.portraitHeight = params.portraitHeight;
    std::copy_n(params.panelColor, 4, internal.panelColor);
    std::copy_n(params.outlineColor, 4, internal.outlineColor);
    std::copy_n(params.accentColor, 4, internal.accentColor);
    std::copy_n(params.barValues, 3, internal.barValues);
    impl_->renderer.render(cmd, width, height, internal);
}

std::expected<void, std::string> RawHud::recreate(const CreateInfo& info)
{
    auto renderer = environment::HudRenderer::create({
        .instance = info.instance, .device = info.device,
        .physicalDevice = info.physicalDevice, .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .shaderDirectory = info.shaderDirectory});
    if (!renderer) return std::unexpected(renderer.error());
    impl_->renderer = std::move(*renderer);
    return {};
}

} // namespace starforge::public_api_bridge
