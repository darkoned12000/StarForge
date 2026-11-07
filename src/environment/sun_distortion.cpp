#include "sun_distortion.hpp"

#include <utility>

namespace starforge::environment
{

std::expected<SunDistortionEffect, std::string> SunDistortionEffect::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("SunDistortionEffect: invalid device");
    }

    SunDistortionEffect effect;
    effect.device_ = info.device;

    // TODO(M8): create sun distortion shader modules and pipeline here.

    return effect;
}

SunDistortionEffect::~SunDistortionEffect()
{
    destroy();
}

SunDistortionEffect::SunDistortionEffect(SunDistortionEffect&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      pipelineLayout_(std::exchange(other.pipelineLayout_, VK_NULL_HANDLE)),
      pipeline_(std::exchange(other.pipeline_, std::nullopt)),
      descriptorPool_(std::exchange(other.descriptorPool_, VK_NULL_HANDLE))
{
}

SunDistortionEffect& SunDistortionEffect::operator=(SunDistortionEffect&& other) noexcept
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

void SunDistortionEffect::destroy() noexcept
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

void SunDistortionEffect::render(VkCommandBuffer /*cmd*/, const SunDistortionRenderContext& /*context*/, const SunDistortionParams& /*params*/)
{
    // TODO(M8): record sun distortion draw commands here.
}

} // namespace starforge::environment
