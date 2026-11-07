#include "wormhole_effect.hpp"

#include <utility>

namespace starforge::environment
{

std::expected<WormholeEffect, std::string> WormholeEffect::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("WormholeEffect: invalid device");
    }

    WormholeEffect effect;
    effect.device_ = info.device;

    // TODO(M8): create wormhole shader modules and pipeline here.

    return effect;
}

WormholeEffect::~WormholeEffect()
{
    destroy();
}

WormholeEffect::WormholeEffect(WormholeEffect&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      pipelineLayout_(std::exchange(other.pipelineLayout_, VK_NULL_HANDLE)),
      pipeline_(std::exchange(other.pipeline_, std::nullopt)),
      descriptorPool_(std::exchange(other.descriptorPool_, VK_NULL_HANDLE))
{
}

WormholeEffect& WormholeEffect::operator=(WormholeEffect&& other) noexcept
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

void WormholeEffect::destroy() noexcept
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

void WormholeEffect::render(VkCommandBuffer /*cmd*/, const WormholeRenderContext& /*context*/, const WormholeParams& /*params*/)
{
    // TODO(M8): record wormhole draw commands here.
}

} // namespace starforge::environment
