#include "storm_effect.hpp"

#include "internal/effect_pipeline.hpp"

#include <cstddef>
#include <utility>

namespace starforge::environment
{

namespace
{

// Must match PushConstants in storm.slang (104 bytes).
struct PushConstants
{
    float time = 0.0f;
    float intensity = 1.0f;
    float seed = 0.0f;
    float wind = 1.0f;
    math::Vec4f color{0.45f, 0.5f, 0.55f, 1.0f};
    math::Vec2f resolution{1.0f, 1.0f};
    alignas(16) math::Mat4f invViewProj{1.0f};
};

static_assert(sizeof(PushConstants) == 112, "Storm push constants must be 112 bytes");

} // namespace

std::expected<StormEffect, std::string> StormEffect::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("StormEffect: invalid device");
    }
    if (info.colorFormat == VK_FORMAT_UNDEFINED)
    {
        return std::unexpected("StormEffect: colorFormat is VK_FORMAT_UNDEFINED");
    }
    if (info.shaderDirectory.empty())
    {
        return std::unexpected("StormEffect: shaderDirectory is empty");
    }

    StormEffect effect;
    effect.device_ = info.device;

    auto inner = internal::FullscreenEffect::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .shaderDirectory = info.shaderDirectory,
        .shaderBaseName = "storm",
        .pushConstantSize = sizeof(PushConstants),
    });
    if (!inner)
    {
        return std::unexpected(std::move(inner.error()));
    }
    effect.effect_ = std::move(*inner);

    return effect;
}

StormEffect::~StormEffect()
{
    destroy();
}

StormEffect::StormEffect(StormEffect&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      effect_(std::exchange(other.effect_, std::nullopt))
{
}

StormEffect& StormEffect::operator=(StormEffect&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        effect_ = std::exchange(other.effect_, std::nullopt);
    }
    return *this;
}

void StormEffect::destroy() noexcept
{
    effect_.reset();
    device_ = VK_NULL_HANDLE;
}

void StormEffect::render(VkCommandBuffer cmd, const StormRenderContext& context, const StormParams& params)
{
    if (!effect_.has_value())
    {
        return;
    }
    const PushConstants push{
        .time = params.time,
        .intensity = params.intensity,
        .seed = 0.0f,
        .wind = params.wind,
        .color = params.color,
        .resolution = context.resolution,
        .invViewProj = context.invViewProj,
    };
    effect_->render(cmd, &push, sizeof(push));
}

} // namespace starforge::environment
