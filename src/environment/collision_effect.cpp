#include "collision_effect.hpp"

#include "internal/effect_pipeline.hpp"

#include <algorithm>
#include <utility>

namespace starforge::environment
{

std::expected<CollisionEffect, std::string> CollisionEffect::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("CollisionEffect: invalid device");
    }
    if (info.colorFormat == VK_FORMAT_UNDEFINED)
    {
        return std::unexpected("CollisionEffect: colorFormat is VK_FORMAT_UNDEFINED");
    }
    if (info.shaderDirectory.empty())
    {
        return std::unexpected("CollisionEffect: shaderDirectory is empty");
    }

    CollisionEffect effect;
    effect.device_ = info.device;

    auto inner = internal::BillboardEffect::create({
        .device = info.device,
        .colorFormat = info.colorFormat,
        .depthFormat = info.depthFormat,
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

CollisionEffect::~CollisionEffect()
{
    destroy();
}

CollisionEffect::CollisionEffect(CollisionEffect&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      effect_(std::exchange(other.effect_, std::nullopt))
{
}

CollisionEffect& CollisionEffect::operator=(CollisionEffect&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        effect_ = std::exchange(other.effect_, std::nullopt);
    }
    return *this;
}

void CollisionEffect::destroy() noexcept
{
    effect_.reset();
    device_ = VK_NULL_HANDLE;
}

void CollisionEffect::render(VkCommandBuffer cmd, const CollisionRenderContext& context, const CollisionParams& params)
{
    if (!effect_.has_value())
    {
        return;
    }
    const math::Vec3f ndc = internal::projectToNdc(context.viewProj, params.position);
    // Expanding shockwave: ring grows with time, intensity decays.
    const float grow = 1.0f + params.time * 2.0f;
    internal::BillboardPush push;
    push.centerSize = {ndc.x, ndc.y,
                       internal::ndcHalfSize(context.viewProj, params.position, params.radius) * grow,
                       context.resolution.x / std::max(context.resolution.y, 1.0f)};
    push.color = params.color;
    push.time = params.time;
    push.intensity = params.intensity / grow;
    push.shape = static_cast<std::uint32_t>(internal::BillboardShape::Ring);
    effect_->render(cmd, &push, sizeof(push));
}

} // namespace starforge::environment
