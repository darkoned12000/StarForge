#include "afterburner_effect.hpp"

#include <utility>

namespace starforge::environment
{

std::expected<AfterburnerEffect, std::string> AfterburnerEffect::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("AfterburnerEffect: invalid device");
    }

    AfterburnerEffect effect;
    effect.device_ = info.device;

    // TODO(M8): create afterburner shader modules and pipeline here.

    return effect;
}

AfterburnerEffect::~AfterburnerEffect()
{
    destroy();
}

AfterburnerEffect::AfterburnerEffect(AfterburnerEffect&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      pipelineLayout_(std::exchange(other.pipelineLayout_, VK_NULL_HANDLE)),
      pipeline_(std::exchange(other.pipeline_, std::nullopt)),
      descriptorPool_(std::exchange(other.descriptorPool_, VK_NULL_HANDLE))
{
}

AfterburnerEffect& AfterburnerEffect::operator=(AfterburnerEffect&& other) noexcept
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

void AfterburnerEffect::destroy() noexcept
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

void AfterburnerEffect::render(VkCommandBuffer /*cmd*/, const AfterburnerRenderContext& /*context*/, const AfterburnerParams& /*params*/)
{
    // TODO(M8): record afterburner draw commands here.
}

} // namespace starforge::environment
