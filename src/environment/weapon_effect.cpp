#include "weapon_effect.hpp"

#include "internal/effect_pipeline.hpp"

#include <algorithm>
#include <utility>

namespace starforge::environment
{

std::expected<WeaponEffect, std::string> WeaponEffect::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("WeaponEffect: invalid device");
    }
    if (info.colorFormat == VK_FORMAT_UNDEFINED)
    {
        return std::unexpected("WeaponEffect: colorFormat is VK_FORMAT_UNDEFINED");
    }
    if (info.shaderDirectory.empty())
    {
        return std::unexpected("WeaponEffect: shaderDirectory is empty");
    }

    WeaponEffect effect;
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

WeaponEffect::~WeaponEffect()
{
    destroy();
}

WeaponEffect::WeaponEffect(WeaponEffect&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      effect_(std::exchange(other.effect_, std::nullopt))
{
}

WeaponEffect& WeaponEffect::operator=(WeaponEffect&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        effect_ = std::exchange(other.effect_, std::nullopt);
    }
    return *this;
}

void WeaponEffect::destroy() noexcept
{
    effect_.reset();
    device_ = VK_NULL_HANDLE;
}

void WeaponEffect::render(VkCommandBuffer cmd, const WeaponRenderContext& context, const WeaponParams& params)
{
    if (!effect_.has_value())
    {
        return;
    }
    const math::Vec3f ndc = internal::projectToNdc(context.viewProj, params.position);
    internal::BillboardPush push;
    push.centerSize = {ndc.x, ndc.y, internal::ndcHalfSize(context.viewProj, params.position, params.size), context.resolution.x / std::max(context.resolution.y, 1.0f)};
    push.color = params.color;
    push.time = params.time;
    push.intensity = params.intensity;
    push.shape = static_cast<std::uint32_t>(internal::BillboardShape::Beam);
    push.rotationAngle = internal::projectedAngle(context.viewProj, params.position, params.direction);
    effect_->render(cmd, &push, sizeof(push));
}

} // namespace starforge::environment
