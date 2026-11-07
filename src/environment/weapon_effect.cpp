#include "weapon_effect.hpp"

#include <utility>

namespace starforge::environment
{

std::expected<WeaponEffect, std::string> WeaponEffect::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("WeaponEffect: invalid device");
    }

    WeaponEffect effect;
    effect.device_ = info.device;

    // TODO(M8): create weapon shader modules and pipeline here.

    return effect;
}

WeaponEffect::~WeaponEffect()
{
    destroy();
}

WeaponEffect::WeaponEffect(WeaponEffect&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      pipelineLayout_(std::exchange(other.pipelineLayout_, VK_NULL_HANDLE)),
      pipeline_(std::exchange(other.pipeline_, std::nullopt)),
      descriptorPool_(std::exchange(other.descriptorPool_, VK_NULL_HANDLE))
{
}

WeaponEffect& WeaponEffect::operator=(WeaponEffect&& other) noexcept
{
    if (this != &other)
    {
        destroy();

        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        pipelineLayout_ = std::exchange(other.pipelineLayout_, VK_NULL_HANDLE);
        pipeline_ = std::exchange(other.pipeline_, std::nullopt);
        descriptorPool_ = std::exchange(other.descriptorPool_, VK_NULL_HANDLE);
    }
    return *this;
}

void WeaponEffect::destroy() noexcept
{
    pipeline_.reset();
    if (device_ != VK_NULL_HANDLE && pipelineLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    }
    if (device_ != VK_NULL_HANDLE && descriptorPool_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    }
    pipelineLayout_ = VK_NULL_HANDLE;
    descriptorPool_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
}

void WeaponEffect::render(VkCommandBuffer /*cmd*/, const WeaponRenderContext& /*context*/, const WeaponParams& /*params*/)
{
    // TODO(M8): record weapon draw commands here.
}

} // namespace starforge::environment
