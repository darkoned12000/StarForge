#include "shield_effect.hpp"

#include "internal/effect_pipeline.hpp"

#include <algorithm>
#include <utility>

namespace starforge::environment
{

std::expected<ShieldEffect, std::string> ShieldEffect::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("ShieldEffect: invalid device");
    }
    if (info.colorFormat == VK_FORMAT_UNDEFINED)
    {
        return std::unexpected("ShieldEffect: colorFormat is VK_FORMAT_UNDEFINED");
    }
    if (info.shaderDirectory.empty())
    {
        return std::unexpected("ShieldEffect: shaderDirectory is empty");
    }

    ShieldEffect effect;
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

ShieldEffect::~ShieldEffect()
{
    destroy();
}

ShieldEffect::ShieldEffect(ShieldEffect&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      effect_(std::exchange(other.effect_, std::nullopt))
{
}

ShieldEffect& ShieldEffect::operator=(ShieldEffect&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        effect_ = std::exchange(other.effect_, std::nullopt);
    }
    return *this;
}

void ShieldEffect::destroy() noexcept
{
    effect_.reset();
    device_ = VK_NULL_HANDLE;
}

void ShieldEffect::render(VkCommandBuffer cmd, const ShieldRenderContext& context, const ShieldParams& params)
{
    if (!effect_.has_value())
    {
        return;
    }
    const math::Vec3f ndc = internal::projectToNdc(context.viewProj, params.position);
    internal::BillboardPush push;
    push.centerSize = {ndc.x, ndc.y, internal::ndcHalfSize(context.viewProj, params.position, params.radius), context.resolution.x / std::max(context.resolution.y, 1.0f)};
    push.color = params.color;
    push.time = params.time;
    push.intensity = params.intensity;
    push.shape = static_cast<std::uint32_t>(internal::BillboardShape::Ring);
    effect_->render(cmd, &push, sizeof(push));
}

} // namespace starforge::environment
