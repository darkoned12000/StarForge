#include "wormhole_effect.hpp"

#include "internal/effect_pipeline.hpp"

#include <cstddef>
#include <utility>

namespace starforge::environment
{

namespace
{

// Must match PushConstants in wormhole.slang (48 bytes).
struct PushConstants
{
    math::Vec4f color{0.5f, 0.8f, 1.0f, 1.0f};
    math::Vec2f center{0.5f, 0.5f};
    math::Vec2f resolution{1.0f, 1.0f};
    float radius = 0.25f;
    float time = 0.0f;
    float intensity = 1.0f;
    float spin = 0.5f;
};

static_assert(sizeof(PushConstants) == 48, "Wormhole push constants must be 48 bytes");
static_assert(offsetof(PushConstants, radius) == 32, "Wormhole radius offset must be 32");

} // namespace

std::expected<WormholeEffect, std::string> WormholeEffect::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("WormholeEffect: invalid device");
    }
    if (info.colorFormat == VK_FORMAT_UNDEFINED)
    {
        return std::unexpected("WormholeEffect: colorFormat is VK_FORMAT_UNDEFINED");
    }
    if (info.shaderDirectory.empty())
    {
        return std::unexpected("WormholeEffect: shaderDirectory is empty");
    }

    WormholeEffect effect;
    effect.device_ = info.device;

    auto inner = internal::FullscreenEffect::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .shaderDirectory = info.shaderDirectory,
        .shaderBaseName = "wormhole",
        .pushConstantSize = sizeof(PushConstants),
    });
    if (!inner)
    {
        return std::unexpected(std::move(inner.error()));
    }
    effect.effect_ = std::move(*inner);

    return effect;
}

WormholeEffect::~WormholeEffect()
{
    destroy();
}

WormholeEffect::WormholeEffect(WormholeEffect&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      effect_(std::exchange(other.effect_, std::nullopt))
{
}

WormholeEffect& WormholeEffect::operator=(WormholeEffect&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        effect_ = std::exchange(other.effect_, std::nullopt);
    }
    return *this;
}

void WormholeEffect::destroy() noexcept
{
    effect_.reset();
    device_ = VK_NULL_HANDLE;
}

void WormholeEffect::render(VkCommandBuffer cmd, const WormholeRenderContext& context, const WormholeParams& params)
{
    if (!effect_.has_value())
    {
        return;
    }
    // Project the 3D wormhole position into screen UVs; cull when behind
    // the camera (unless the caller left the default origin position,
    // which means "screen center" for backward compatibility).
    const math::Vec4f clip = context.viewProj * math::Vec4f{params.position, 1.0f};
    if (clip.w <= 1e-3f && params.position != math::Vec3f{0.0f, 0.0f, 0.0f})
    {
        return;
    }

    math::Vec2f center{0.5f, 0.5f};
    float screenRadius = 0.25f;
    if (clip.w > 1e-3f)
    {
        center.x = (clip.x / clip.w) * 0.5f + 0.5f;
        center.y = (clip.y / clip.w) * 0.5f + 0.5f;
        screenRadius = internal::ndcHalfSize(context.viewProj, params.position, params.radius);
    }

    const PushConstants push{
        .color = params.color,
        .center = center,
        .resolution = context.resolution,
        .radius = screenRadius,
        .time = params.time,
        .intensity = params.intensity,
        .spin = params.spin,
    };
    effect_->render(cmd, &push, sizeof(push));
}

} // namespace starforge::environment
