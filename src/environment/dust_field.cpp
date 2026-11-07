#include "dust_field.hpp"

#include <utility>

namespace starforge::environment
{

std::expected<DustFieldRenderer, std::string> DustFieldRenderer::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("DustFieldRenderer: invalid device");
    }

    DustFieldRenderer renderer;
    renderer.device_ = info.device;

    // TODO(M8): create dust shader modules and volumetric pipeline here.

    return renderer;
}

DustFieldRenderer::~DustFieldRenderer()
{
    destroy();
}

DustFieldRenderer::DustFieldRenderer(DustFieldRenderer&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      pipelineLayout_(std::exchange(other.pipelineLayout_, VK_NULL_HANDLE)),
      pipeline_(std::exchange(other.pipeline_, std::nullopt)),
      descriptorPool_(std::exchange(other.descriptorPool_, VK_NULL_HANDLE))
{
}

DustFieldRenderer& DustFieldRenderer::operator=(DustFieldRenderer&& other) noexcept
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

void DustFieldRenderer::destroy() noexcept
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

void DustFieldRenderer::render(VkCommandBuffer /*cmd*/, const DustFieldRenderContext& /*context*/, const DustFieldParams& /*params*/)
{
    // TODO(M8): record volumetric dust commands here.
}

} // namespace starforge::environment
