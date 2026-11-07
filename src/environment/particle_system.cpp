#include "particle_system.hpp"

#include <utility>

namespace starforge::environment
{

std::expected<ParticleSystem, std::string> ParticleSystem::create(const CreateInfo& info)
{
    if (info.device == VK_NULL_HANDLE)
    {
        return std::unexpected("ParticleSystem: invalid device");
    }

    ParticleSystem system;
    system.device_ = info.device;

    // TODO(M8): initialize particle buffers, shaders, and pipeline here.

    return system;
}

ParticleSystem::~ParticleSystem()
{
    destroy();
}

ParticleSystem::ParticleSystem(ParticleSystem&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      pipelineLayout_(std::exchange(other.pipelineLayout_, VK_NULL_HANDLE)),
      pipeline_(std::exchange(other.pipeline_, std::nullopt)),
      descriptorPool_(std::exchange(other.descriptorPool_, VK_NULL_HANDLE))
{
}

ParticleSystem& ParticleSystem::operator=(ParticleSystem&& other) noexcept
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

void ParticleSystem::destroy() noexcept
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

void ParticleSystem::update(float /*deltaTime*/)
{
    // TODO(M8): update particle physics and states.
}

void ParticleSystem::render(VkCommandBuffer /*cmd*/, const ParticleRenderContext& /*context*/, const ParticleParams& /*params*/)
{
    // TODO(M8): record particle draw commands here.
}

} // namespace starforge::environment
