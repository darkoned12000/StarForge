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

struct ThrusterRenderContext
{
    math::Mat4f viewProj{};
    math::Vec3f cameraPosition{0.0f, 0.0f, 0.0f};
    math::Vec2f resolution{1.0f, 1.0f};
};

// Thruster effect parameters (ship engine exhaust plume).
struct ThrusterParams
{
    math::Vec3f position{0.0f, 0.0f, 0.0f};
    math::Vec3f direction{0.0f, 0.0f, 1.0f}; // Direction exhaust points
    float intensity = 1.0f;
    float length = 1.0f;
    float width = 0.2f;
    math::Vec4f color{1.0f, 0.7f, 0.3f, 1.0f}; // Orange/Red default
    float time = 0.0f;
};

// Thruster effect system for ship engines and thrust effects (rendering pending).
class ThrusterEffect
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

    [[nodiscard]] static std::expected<ThrusterEffect, std::string> create(const CreateInfo& info);

    ~ThrusterEffect();

    ThrusterEffect(const ThrusterEffect&) = delete;
    ThrusterEffect& operator=(const ThrusterEffect&) = delete;

    ThrusterEffect(ThrusterEffect&& other) noexcept;
    ThrusterEffect& operator=(ThrusterEffect&& other) noexcept;

    // Render a thruster effect
    void render(VkCommandBuffer cmd, const ThrusterRenderContext& context, const ThrusterParams& params);

private:
    ThrusterEffect() = default;
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    std::optional<vulkan::GraphicsPipeline> pipeline_;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
};

} // namespace starforge::environment
