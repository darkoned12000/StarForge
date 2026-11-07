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

struct AfterburnerRenderContext
{
    math::Mat4f viewProj{};
    math::Vec3f cameraPosition{0.0f, 0.0f, 0.0f};
    math::Vec2f resolution{1.0f, 1.0f};
};

struct AfterburnerParams
{
    math::Vec3f position{0.0f, 0.0f, 0.0f};
    math::Vec3f direction{0.0f, 0.0f, -1.0f}; // Direction exhaust points
    float length = 1.0f;
    float width = 0.3f;
    math::Vec4f color{1.0f, 0.6f, 0.2f, 1.0f}; // Orange/Red default
    float intensity = 1.0f;
    float time = 0.0f;
};

// Afterburner effect system for ship boost effects (rendering pending).
class AfterburnerEffect
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

    [[nodiscard]] static std::expected<AfterburnerEffect, std::string> create(const CreateInfo& info);

    ~AfterburnerEffect();

    AfterburnerEffect(const AfterburnerEffect&) = delete;
    AfterburnerEffect& operator=(const AfterburnerEffect&) = delete;

    AfterburnerEffect(AfterburnerEffect&& other) noexcept;
    AfterburnerEffect& operator=(AfterburnerEffect&& other) noexcept;

    // Render an afterburner effect
    void render(VkCommandBuffer cmd, const AfterburnerRenderContext& context, const AfterburnerParams& params);

private:
    AfterburnerEffect() = default;
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    std::optional<internal::BillboardEffect> effect_;
};

} // namespace starforge::environment
