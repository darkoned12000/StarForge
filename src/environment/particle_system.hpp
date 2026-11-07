#pragma once

#include "math/math.hpp"
#include "vulkan/vulkan_pipeline.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace starforge::environment
{

struct ParticleRenderContext
{
    math::Mat4f viewProj{};
    math::Vec3f cameraPosition{0.0f, 0.0f, 0.0f};
    math::Vec2f resolution{1.0f, 1.0f};
};

// Particle system parameters (emitter-driven GPU particles).
struct ParticleParams
{
    math::Vec3f emitterPosition{0.0f, 0.0f, 0.0f};
    math::Vec3f velocity{0.0f, 0.0f, -1.0f};
    float lifetime = 5.0f;
    float size = 1.0f;
    float speed = 1.0f;
    math::Vec4f color{1.0f, 1.0f, 1.0f, 1.0f}; // White default
    std::uint32_t count = 1000;
};

// Particle system for effects like weapons and shields (rendering pending).
class ParticleSystem
{
public:
    struct CreateInfo
    {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        std::uint32_t queueFamily = 0;
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
        std::string shaderDirectory{};
    };

    [[nodiscard]] static std::expected<ParticleSystem, std::string> create(const CreateInfo& info);

    ~ParticleSystem();

    ParticleSystem(const ParticleSystem&) = delete;
    ParticleSystem& operator=(const ParticleSystem&) = delete;

    ParticleSystem(ParticleSystem&& other) noexcept;
    ParticleSystem& operator=(ParticleSystem&& other) noexcept;

    // Update particle system (CPU-side simulation step).
    void update(float deltaTime);

    // Render particles (requires particle state buffer; pending).
    void render(VkCommandBuffer cmd, const ParticleRenderContext& context, const ParticleParams& params);

private:
    ParticleSystem() = default;
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    std::optional<vulkan::GraphicsPipeline> pipeline_;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
};

} // namespace starforge::environment
