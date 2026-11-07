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

struct WormholeRenderContext
{
    VkImageView sceneColorView = VK_NULL_HANDLE;
    VkImageView sceneDepthView = VK_NULL_HANDLE;
    math::Mat4f viewProj{};
    math::Vec3f cameraPosition{0.0f, 0.0f, 0.0f};
    math::Vec2f resolution{1.0f, 1.0f};
};

struct WormholeParams
{
    math::Vec3f position{0.0f, 0.0f, 0.0f};
    float radius = 1.0f;
    float length = 2.0f;
    float spin = 0.5f;
    math::Vec4f color{0.5f, 0.8f, 1.0f, 1.0f}; // Blue/teal default
    float intensity = 1.0f;
    float time = 0.0f;
};

// Wormhole effect system for space distortion effects (rendering pending).
class WormholeEffect
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

    [[nodiscard]] static std::expected<WormholeEffect, std::string> create(const CreateInfo& info);

    ~WormholeEffect();

    WormholeEffect(const WormholeEffect&) = delete;
    WormholeEffect& operator=(const WormholeEffect&) = delete;

    WormholeEffect(WormholeEffect&& other) noexcept;
    WormholeEffect& operator=(WormholeEffect&& other) noexcept;

    // Render a wormhole effect
    void render(VkCommandBuffer cmd, const WormholeRenderContext& context, const WormholeParams& params);

private:
    WormholeEffect() = default;
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    std::optional<vulkan::GraphicsPipeline> pipeline_;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
};

} // namespace starforge::environment
