#pragma once

#include "internal/effect_pipeline.hpp"
#include "math/math.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace starforge::environment
{

struct SunDistortionRenderContext
{
    // Sun center in [0, 1] UV space (origin bottom-left, y up, matching
    // the fullscreen vertex shader's uv).
    math::Vec2f sunScreenPos{0.5f, 0.5f};
    // Sun screen radius as a fraction of the shorter framebuffer edge.
    float sunRadiusScreen = 0.1f;
    // Framebuffer resolution in pixels (for aspect-correct distortion).
    math::Vec2f resolution{1.0f, 1.0f};
    // Reserved for a future framebuffer-texture refraction pass; unused
    // (glow+ring approximation) until descriptor plumbing exists.
    VkImageView sceneColorView = VK_NULL_HANDLE;
    VkImageView sceneDepthView = VK_NULL_HANDLE;
    math::Mat4f viewProj{};
    math::Vec3f cameraPosition{0.0f, 0.0f, 0.0f};
};

struct SunDistortionParams
{
    math::Vec3f worldPosition{0.0f, 0.0f, 0.0f};
    float radius = 1.0f;
    math::Vec4f color{1.0f, 0.9f, 0.5f, 1.0f}; // Yellow/Orange default
    float intensity = 1.0f;
    float time = 0.0f;
};

// Sun distortion effect system for visual effects when approaching stars/suns (rendering pending).
class SunDistortionEffect
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

    [[nodiscard]] static std::expected<SunDistortionEffect, std::string> create(const CreateInfo& info);

    ~SunDistortionEffect();

    SunDistortionEffect(const SunDistortionEffect&) = delete;
    SunDistortionEffect& operator=(const SunDistortionEffect&) = delete;

    SunDistortionEffect(SunDistortionEffect&& other) noexcept;
    SunDistortionEffect& operator=(SunDistortionEffect&& other) noexcept;

    // Render a sun distortion effect
    void render(VkCommandBuffer cmd, const SunDistortionRenderContext& context, const SunDistortionParams& params);

private:
    SunDistortionEffect() = default;
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    std::optional<internal::FullscreenEffect> effect_;
};

} // namespace starforge::environment
