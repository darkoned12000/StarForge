#include "thruster_effect.hpp"

#include <utility>

namespace starforge::environment
{

std::expected<ThrusterEffect, std::string> ThrusterEffect::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("ThrusterEffect: invalid device");
    }

    ThrusterEffect effect;
    effect.device_ = info.device;

    // TODO(M8): create thruster shader modules and pipeline here.

    return effect;
}

ThrusterEffect::~ThrusterEffect()
{
    destroy();
}

ThrusterEffect::ThrusterEffect(ThrusterEffect&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      pipelineLayout_(std::exchange(other.pipelineLayout_, VK_NULL_HANDLE)),
      pipeline_(std::exchange(other.pipeline_, std::nullopt)),
      descriptorPool_(std::exchange(other.descriptorPool_, VK_NULL_HANDLE))
{
}

ThrusterEffect& ThrusterEffect::operator=(ThrusterEffect&& other) noexcept
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

void ThrusterEffect::destroy() noexcept
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

void ThrusterEffect::render(VkCommandBuffer /*cmd*/, const ThrusterRenderContext& /*context*/, const ThrusterParams& /*params*/)
{
    // TODO(M8): record thruster draw commands here.
}

} // namespace starforge::environment
