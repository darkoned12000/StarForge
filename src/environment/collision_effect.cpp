#include "collision_effect.hpp"

#include <utility>

namespace starforge::environment
{

std::expected<CollisionEffect, std::string> CollisionEffect::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("CollisionEffect: invalid device");
    }

    CollisionEffect effect;
    effect.device_ = info.device;

    // TODO(M8): create collision shader modules and pipeline here.

    return effect;
}

CollisionEffect::~CollisionEffect()
{
    destroy();
}

CollisionEffect::CollisionEffect(CollisionEffect&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      pipelineLayout_(std::exchange(other.pipelineLayout_, VK_NULL_HANDLE)),
      pipeline_(std::exchange(other.pipeline_, std::nullopt)),
      descriptorPool_(std::exchange(other.descriptorPool_, VK_NULL_HANDLE))
{
}

CollisionEffect& CollisionEffect::operator=(CollisionEffect&& other) noexcept
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

void CollisionEffect::destroy() noexcept
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

void CollisionEffect::render(VkCommandBuffer /*cmd*/, const CollisionRenderContext& /*context*/, const CollisionParams& /*params*/)
{
    // TODO(M8): record collision effect draw commands here.
}

} // namespace starforge::environment
