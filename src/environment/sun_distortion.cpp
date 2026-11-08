#include "sun_distortion.hpp"

#include "internal/effect_pipeline.hpp"

#include <cstddef>
#include <utility>

namespace starforge::environment
{

namespace
{

// Must match PushConstants in sun_distortion.slang (std430: float2 is
// 8-byte aligned, so sunPos starts at offset 16; total 32 bytes).
struct PushConstants
{
    float time = 0.0f;
    float intensity = 1.0f;
    float sunRadius = 0.1f;
    float sunPad = 0.0f;
    math::Vec2f sunPos{0.5f, 0.5f};
    math::Vec2f resolution{1.0f, 1.0f};
};

static_assert(sizeof(PushConstants) == 32, "Sun distortion push constants must be 32 bytes");
static_assert(offsetof(PushConstants, sunPos) == 16, "Sun position offset must be 16");

} // namespace

std::expected<SunDistortionEffect, std::string> SunDistortionEffect::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("SunDistortionEffect: invalid device");
    }
    if (info.colorFormat == VK_FORMAT_UNDEFINED)
    {
        return std::unexpected("SunDistortionEffect: colorFormat is VK_FORMAT_UNDEFINED");
    }
    if (info.shaderDirectory.empty())
    {
        return std::unexpected("SunDistortionEffect: shaderDirectory is empty");
    }

    SunDistortionEffect effect;
    effect.device_ = info.device;

    auto inner = internal::FullscreenEffect::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
        .shaderDirectory = info.shaderDirectory,
        .shaderBaseName = "sun_distortion",
        .pushConstantSize = sizeof(PushConstants),
    });
    if (!inner)
    {
        return std::unexpected(std::move(inner.error()));
    }
    effect.effect_ = std::move(*inner);

    return effect;
}

SunDistortionEffect::~SunDistortionEffect()
{
    destroy();
}

SunDistortionEffect::SunDistortionEffect(SunDistortionEffect&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      effect_(std::exchange(other.effect_, std::nullopt))
{
}

SunDistortionEffect& SunDistortionEffect::operator=(SunDistortionEffect&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        effect_ = std::exchange(other.effect_, std::nullopt);
    }
    return *this;
}

void SunDistortionEffect::destroy() noexcept
{
    effect_.reset();
    device_ = VK_NULL_HANDLE;
}

void SunDistortionEffect::render(VkCommandBuffer cmd, const SunDistortionRenderContext& context, const SunDistortionParams& params)
{
    if (!effect_.has_value())
    {
        return;
    }
    // Prefer the 3D world position (projected to UV); fall back to the
    // context screen position only when no world position was supplied.
    math::Vec2f screenPos = context.sunScreenPos;
    float screenRadius = context.sunRadiusScreen;

    const math::Vec4f clip = context.viewProj * math::Vec4f{params.worldPosition, 1.0f};
    if (clip.w > 1e-3f)
    {
        screenPos.x = (clip.x / clip.w) * 0.5f + 0.5f;
        screenPos.y = (clip.y / clip.w) * 0.5f + 0.5f;
        screenRadius = internal::ndcHalfSize(context.viewProj, params.worldPosition, params.radius);
    }
    else if (params.worldPosition != math::Vec3f{0.0f, 0.0f, 0.0f})
    {
        return; // Sun is behind camera; cull effect.
    }

    const PushConstants push{
        .time = params.time,
        .intensity = params.intensity,
        .sunRadius = screenRadius,
        .sunPad = 0.0f,
        .sunPos = screenPos,
        .resolution = context.resolution,
    };
    effect_->render(cmd, &push, sizeof(push));
}

} // namespace starforge::environment
