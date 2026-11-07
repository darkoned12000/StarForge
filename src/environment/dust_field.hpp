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

// Camera/volumetric context for raymarched dust (mirrors NebulaRenderContext).
struct DustFieldRenderContext
{
    math::Mat4f invViewProj{};
    math::Vec3f cameraPosition{0.0f, 0.0f, 0.0f};
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    math::Vec2f resolution{1.0f, 1.0f};
    VkImageView sceneDepthView = VK_NULL_HANDLE; // For depth testing against raymarch steps
};

// Dust field rendering parameters (volumetric extinction model).
struct DustFieldParams
{
    float density = 1.0f;
    float scattering = 0.8f;
    float absorption = 0.2f;
    float time = 0.0f;
    math::Vec4f color{0.8f, 0.7f, 0.5f, 1.0f}; // Beige/tan default
};

// Dust field rendering system (rendering pending).
class DustFieldRenderer
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

    [[nodiscard]] static std::expected<DustFieldRenderer, std::string> create(const CreateInfo& info);

    ~DustFieldRenderer();

    DustFieldRenderer(const DustFieldRenderer&) = delete;
    DustFieldRenderer& operator=(const DustFieldRenderer&) = delete;

    DustFieldRenderer(DustFieldRenderer&& other) noexcept;
    DustFieldRenderer& operator=(DustFieldRenderer&& other) noexcept;

    // Render a dust field effect
    void render(VkCommandBuffer cmd, const DustFieldRenderContext& context, const DustFieldParams& params);

private:
    DustFieldRenderer() = default;
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    std::optional<vulkan::GraphicsPipeline> pipeline_;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
};

} // namespace starforge::environment
