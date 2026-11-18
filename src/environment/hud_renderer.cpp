#include "hud_renderer.hpp"

#include "environment/internal/texture_effect.hpp"
#include "gpu_resources/allocator.hpp"
#include "gpu_resources/image.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace starforge::environment
{

namespace
{
struct HudPush
{
    float panel[4], portrait[4], panelColor[4], outlineColor[4], accentColor[4], screen[4];
};
static_assert(sizeof(HudPush) == 96);
} // namespace

struct HudRenderer::Impl
{
    std::unique_ptr<gpu_resources::Allocator> allocator;
    std::unique_ptr<gpu_resources::Image> whiteImage;
    gpu_resources::Image::ImageView whiteView;
    std::unique_ptr<internal::TextureEffect> effect;
};

HudRenderer::~HudRenderer() = default;
HudRenderer::HudRenderer(HudRenderer&&) noexcept = default;
HudRenderer& HudRenderer::operator=(HudRenderer&&) noexcept = default;

std::expected<HudRenderer, std::string> HudRenderer::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE || info.instance == VK_NULL_HANDLE)
        return std::unexpected("HudRenderer requires an instance and device");

    auto allocator = gpu_resources::Allocator::create({
        .instance = info.instance, .physicalDevice = info.physicalDevice, .device = info.device});
    if (!allocator) return std::unexpected(allocator.error());

    auto image = gpu_resources::Image::create(*allocator, {
        .width = 1, .height = 1, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .usage = gpu_resources::ImageUsageFlags::Sampled | gpu_resources::ImageUsageFlags::TransferDst});
    if (!image) return std::unexpected(image.error());
    auto view = image->createImageView(info.device);
    if (!view) return std::unexpected(view.error());

    auto effect = internal::TextureEffect::create({
        .device = info.device, .colorFormat = info.colorFormat,
        .shaderDirectory = info.shaderDirectory, .shaderBaseName = "hud",
        .pushConstantSize = sizeof(HudPush)});
    if (!effect) return std::unexpected(effect.error());

    HudRenderer renderer;
    renderer.impl_ = std::make_unique<Impl>();
    renderer.impl_->allocator = std::make_unique<gpu_resources::Allocator>(std::move(*allocator));
    renderer.impl_->whiteImage = std::make_unique<gpu_resources::Image>(std::move(*image));
    renderer.impl_->whiteView = std::move(*view);
    renderer.impl_->effect = std::make_unique<internal::TextureEffect>(std::move(*effect));
    return renderer;
}

void HudRenderer::render(VkCommandBuffer cmd, float width, float height, const HudParams& params) const
{
    if (!impl_) return;
    HudPush push{};
    push.panel[0] = params.panelX;
    push.panel[1] = params.panelY;
    push.panel[2] = params.panelWidth;
    push.panel[3] = params.panelHeight;
    push.portrait[0] = params.portraitX;
    push.portrait[1] = params.portraitY;
    push.portrait[2] = params.portraitWidth;
    push.portrait[3] = params.portraitHeight;
    std::copy_n(params.panelColor, 4, push.panelColor);
    std::copy_n(params.outlineColor, 4, push.outlineColor);
    std::copy_n(params.accentColor, 4, push.accentColor);
    push.screen[0] = width;
    push.screen[1] = height;
    impl_->effect->render(cmd, impl_->whiteView.handle(), &push, sizeof(push));
}

} // namespace starforge::environment
