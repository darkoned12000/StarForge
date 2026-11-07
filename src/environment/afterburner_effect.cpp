#include "afterburner_effect.hpp"

#include "internal/effect_pipeline.hpp"

#include <algorithm>
#include <utility>

namespace starforge::environment
{

std::expected<AfterburnerEffect, std::string> AfterburnerEffect::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("AfterburnerEffect: invalid device");
    }
    if (info.colorFormat == VK_FORMAT_UNDEFINED)
    {
        return std::unexpected("AfterburnerEffect: colorFormat is VK_FORMAT_UNDEFINED");
    }
    if (info.shaderDirectory.empty())
    {
        return std::unexpected("AfterburnerEffect: shaderDirectory is empty");
    }

    AfterburnerEffect effect;
    effect.device_ = info.device;

    auto inner = internal::BillboardEffect::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .shaderDirectory = info.shaderDirectory,
        .shaderBaseName = "billboard",
        .pushConstantSize = sizeof(internal::BillboardPush),
    });
    if (!inner)
    {
        return std::unexpected(std::move(inner.error()));
    }
    effect.effect_ = std::move(*inner);

    return effect;
}

AfterburnerEffect::~AfterburnerEffect()
{
    destroy();
}

AfterburnerEffect::AfterburnerEffect(AfterburnerEffect&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      effect_(std::exchange(other.effect_, std::nullopt))
{
}

AfterburnerEffect& AfterburnerEffect::operator=(AfterburnerEffect&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        effect_ = std::exchange(other.effect_, std::nullopt);
    }
    return *this;
}

void AfterburnerEffect::destroy() noexcept
{
    effect_.reset();
    device_ = VK_NULL_HANDLE;
}

void AfterburnerEffect::render(VkCommandBuffer cmd, const AfterburnerRenderContext& context, const AfterburnerParams& params)
{
    if (!effect_.has_value())
    {
        return;
    }
    const math::Vec3f worldPos = params.position + params.direction * (params.length * 0.5f);
    const math::Vec3f ndc = internal::projectToNdc(context.viewProj, worldPos);
    internal::BillboardPush push;
    push.centerSize = {ndc.x, ndc.y, internal::ndcHalfSize(context.viewProj, worldPos, params.width), context.resolution.x / std::max(context.resolution.y, 1.0f)};
    push.color = params.color;
    push.time = params.time;
    push.intensity = params.intensity;
    push.shape = static_cast<std::uint32_t>(internal::BillboardShape::Cone);
    push.rotationAngle = internal::projectedAngle(context.viewProj, worldPos, params.direction);
    effect_->render(cmd, &push, sizeof(push));
}

} // namespace starforge::environment
