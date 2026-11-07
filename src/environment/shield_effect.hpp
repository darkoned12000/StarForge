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

struct ShieldRenderContext
{
    math::Mat4f viewProj{};
    math::Vec3f cameraPosition{0.0f, 0.0f, 0.0f};
    math::Vec2f resolution{1.0f, 1.0f};
};

struct ShieldParams
{
    math::Vec3f position{0.0f, 0.0f, 0.0f};
    float radius = 5.0f;
    math::Vec4f color{0.3f, 0.7f, 1.0f, 1.0f}; // Cyan bubble
    float intensity = 1.0f;
    float time = 0.0f;
};

// Shield bubble effect (billboard ring, screen-space for now).
class ShieldEffect
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

    [[nodiscard]] static std::expected<ShieldEffect, std::string> create(const CreateInfo& info);

    ~ShieldEffect();

    ShieldEffect(const ShieldEffect&) = delete;
    ShieldEffect& operator=(const ShieldEffect&) = delete;

    ShieldEffect(ShieldEffect&& other) noexcept;
    ShieldEffect& operator=(ShieldEffect&& other) noexcept;

    void render(VkCommandBuffer cmd, const ShieldRenderContext& context, const ShieldParams& params);

private:
    ShieldEffect() = default;
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    std::optional<internal::BillboardEffect> effect_;
};

} // namespace starforge::environment
