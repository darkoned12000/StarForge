#include "ftl_effect.hpp"

#include <utility>

namespace starforge::environment
{

std::expected<FtlEffect, std::string> FtlEffect::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("FtlEffect: invalid device");
    }

    FtlEffect effect;
    effect.device_ = info.device;

    // TODO(M8): create FTL shader modules and pipeline here.

    return effect;
}

FtlEffect::~FtlEffect()
{
    destroy();
}

FtlEffect::FtlEffect(FtlEffect&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      pipelineLayout_(std::exchange(other.pipelineLayout_, VK_NULL_HANDLE)),
      pipeline_(std::exchange(other.pipeline_, std::nullopt)),
      descriptorPool_(std::exchange(other.descriptorPool_, VK_NULL_HANDLE))
{
}

FtlEffect& FtlEffect::operator=(FtlEffect&& other) noexcept
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

void FtlEffect::destroy() noexcept
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

void FtlEffect::render(VkCommandBuffer /*cmd*/, const FtlRenderContext& /*context*/, const FtlParams& /*params*/)
{
    // TODO(M8): record FTL effect draw commands here.
}

} // namespace starforge::environment
