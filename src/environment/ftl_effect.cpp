#include "ftl_effect.hpp"

#include "internal/effect_pipeline.hpp"

#include <cstddef>
#include <utility>

namespace starforge::environment
{

namespace
{

// Must match PushConstants in ftl.slang (std430: color @0, resolution
// @16, focalPoint @24, time @32, intensity @36, speed @40, seed @44).
struct PushConstants
{
    math::Vec4f color{0.9f, 0.8f, 1.0f, 1.0f};
    math::Vec2f resolution{1.0f, 1.0f};
    math::Vec2f focalPoint{0.5f, 0.5f}; // Vanishing point from velocityDir
    float time = 0.0f;
    float intensity = 1.0f;
    float speed = 1.0f;
    float seed = 0.0f;
};

static_assert(sizeof(PushConstants) == 48, "FTL push constants must be 48 bytes");
static_assert(offsetof(PushConstants, focalPoint) == 24, "FTL focalPoint offset must be 24");

} // namespace

std::expected<FtlEffect, std::string> FtlEffect::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("FtlEffect: invalid device");
    }
    if (info.colorFormat == VK_FORMAT_UNDEFINED)
    {
        return std::unexpected("FtlEffect: colorFormat is VK_FORMAT_UNDEFINED");
    }
    if (info.shaderDirectory.empty())
    {
        return std::unexpected("FtlEffect: shaderDirectory is empty");
    }

    FtlEffect effect;
    effect.device_ = info.device;

    auto inner = internal::FullscreenEffect::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .shaderDirectory = info.shaderDirectory,
        .shaderBaseName = "ftl",
        .pushConstantSize = sizeof(PushConstants),
    });
    if (!inner)
    {
        return std::unexpected(std::move(inner.error()));
    }
    effect.effect_ = std::move(*inner);

    return effect;
}

FtlEffect::~FtlEffect()
{
    destroy();
}

FtlEffect::FtlEffect(FtlEffect&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      effect_(std::exchange(other.effect_, std::nullopt))
{
}

FtlEffect& FtlEffect::operator=(FtlEffect&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        effect_ = std::exchange(other.effect_, std::nullopt);
    }
    return *this;
}

void FtlEffect::destroy() noexcept
{
    effect_.reset();
    device_ = VK_NULL_HANDLE;
}

void FtlEffect::render(VkCommandBuffer cmd, const FtlRenderContext& context, const FtlParams& params)
{
    if (!effect_.has_value())
    {
        return;
    }
    // Project the ship velocity direction into screen-space UV focus.
    math::Vec2f focalPoint{0.5f, 0.5f};
    const math::Vec4f clipVel = context.viewProj * math::Vec4f{context.cameraPosition + context.velocityDir, 1.0f};
    if (clipVel.w > 1e-4f)
    {
        focalPoint.x = (clipVel.x / clipVel.w) * 0.5f + 0.5f;
        focalPoint.y = (clipVel.y / clipVel.w) * 0.5f + 0.5f;
    }

    const PushConstants push{
        .color = params.color,
        .resolution = context.resolution,
        .focalPoint = focalPoint,
        .time = params.time,
        .intensity = params.intensity,
        .speed = params.speed,
        .seed = 0.0f,
    };
    effect_->render(cmd, &push, sizeof(push));
}

} // namespace starforge::environment
