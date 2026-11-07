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

struct FtlRenderContext
{
    VkImageView sceneColorView = VK_NULL_HANDLE;
    math::Mat4f viewProj{};
    math::Vec3f cameraPosition{0.0f, 0.0f, 0.0f};
    math::Vec3f velocityDir{0.0f, 0.0f, -1.0f};  // Ship movement direction
    math::Vec2f resolution{1.0f, 1.0f};
};

struct FtlParams
{
    float speed = 1.0f;
    float intensity = 1.0f;
    math::Vec4f color{0.9f, 0.8f, 1.0f, 1.0f}; // Purple/indigo default
    float time = 0.0f;
};

// FTL travel effect system for showing rapid movement through space (rendering pending).
class FtlEffect
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

    [[nodiscard]] static std::expected<FtlEffect, std::string> create(const CreateInfo& info);

    ~FtlEffect();

    FtlEffect(const FtlEffect&) = delete;
    FtlEffect& operator=(const FtlEffect&) = delete;

    FtlEffect(FtlEffect&& other) noexcept;
    FtlEffect& operator=(FtlEffect&& other) noexcept;

    // Render an FTL effect
    void render(VkCommandBuffer cmd, const FtlRenderContext& context, const FtlParams& params);

private:
    FtlEffect() = default;
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    std::optional<vulkan::GraphicsPipeline> pipeline_;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
};

} // namespace starforge::environment
