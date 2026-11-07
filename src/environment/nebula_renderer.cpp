#include "nebula_renderer.hpp"

#include <utility>

namespace starforge::environment
{

std::expected<NebulaRenderer, std::string> NebulaRenderer::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("NebulaRenderer: invalid device");
    }

    NebulaRenderer renderer;
    renderer.device_ = info.device;

    // TODO(M8): create shader modules, volumetric pipeline, and noise
    // descriptor pool here. Currently a validated no-op handle.

    return renderer;
}

NebulaRenderer::~NebulaRenderer()
{
    destroy();
}

NebulaRenderer::NebulaRenderer(NebulaRenderer&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      pipelineLayout_(std::exchange(other.pipelineLayout_, VK_NULL_HANDLE)),
      pipeline_(std::exchange(other.pipeline_, std::nullopt)),
      descriptorPool_(std::exchange(other.descriptorPool_, VK_NULL_HANDLE))
{
}

NebulaRenderer& NebulaRenderer::operator=(NebulaRenderer&& other) noexcept
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

void NebulaRenderer::destroy() noexcept
{
    // Pipeline must be released before its layout (see StarfieldRenderer).
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

void NebulaRenderer::render(VkCommandBuffer /*cmd*/, const NebulaRenderContext& /*context*/, const NebulaParams& /*params*/)
{
    // TODO(M8): record volumetric raymarching commands here.
}

} // namespace starforge::environment
