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

struct NebulaRenderContext
{
    math::Mat4f invViewProj{};
    math::Vec3f cameraPosition{0.0f, 0.0f, 0.0f};
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    math::Vec2f resolution{1.0f, 1.0f};
    VkImageView sceneDepthView = VK_NULL_HANDLE; // For depth testing against raymarch steps
};

struct NebulaParams
{
    math::Vec4f baseColor{0.5f, 0.2f, 0.8f, 1.0f};
    float density = 1.0f;
    float absorption = 0.1f;
    float scattering = 0.5f;
    float phaseG = 0.2f;        // Henyey-Greenstein asymmetry factor
    std::uint32_t stepCount = 64;
    float time = 0.0f;
};

// Nebula rendering system (volumetric raymarching; rendering pending).
class NebulaRenderer
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

    [[nodiscard]] static std::expected<NebulaRenderer, std::string> create(const CreateInfo& info);

    ~NebulaRenderer();

    NebulaRenderer(const NebulaRenderer&) = delete;
    NebulaRenderer& operator=(const NebulaRenderer&) = delete;

    NebulaRenderer(NebulaRenderer&& other) noexcept;
    NebulaRenderer& operator=(NebulaRenderer&& other) noexcept;

    void render(VkCommandBuffer cmd, const NebulaRenderContext& context, const NebulaParams& params);

private:
    NebulaRenderer() = default;
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    std::optional<vulkan::GraphicsPipeline> pipeline_;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
};

} // namespace starforge::environment
