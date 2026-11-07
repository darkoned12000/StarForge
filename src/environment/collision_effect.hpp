#pragma once

#include "math/math.hpp"
#include "vulkan/vulkan_pipeline.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace starforge::environment
{

struct CollisionRenderContext
{
    math::Mat4f viewProj{};
    math::Vec3f cameraPosition{0.0f, 0.0f, 0.0f};
    math::Vec2f resolution{1.0f, 1.0f};
};

struct CollisionParams
{
    math::Vec3f position{0.0f, 0.0f, 0.0f};
    float radius = 1.0f;
    math::Vec4f color{1.0f, 0.8f, 0.2f, 1.0f}; // Orange/amber default
    float intensity = 1.0f;
    float time = 0.0f;
};

// Collision effect system for impacts and explosions (rendering pending).
class CollisionEffect
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

    [[nodiscard]] static std::expected<CollisionEffect, std::string> create(const CreateInfo& info);

    ~CollisionEffect();

    CollisionEffect(const CollisionEffect&) = delete;
    CollisionEffect& operator=(const CollisionEffect&) = delete;

    CollisionEffect(CollisionEffect&& other) noexcept;
    CollisionEffect& operator=(CollisionEffect&& other) noexcept;

    // Render a collision effect
    void render(VkCommandBuffer cmd, const CollisionRenderContext& context, const CollisionParams& params);

private:
    CollisionEffect() = default;
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    std::optional<vulkan::GraphicsPipeline> pipeline_;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
};

} // namespace starforge::environment
