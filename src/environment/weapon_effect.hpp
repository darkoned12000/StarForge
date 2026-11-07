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

struct WeaponRenderContext
{
    math::Mat4f viewProj{};
    math::Vec3f cameraPosition{0.0f, 0.0f, 0.0f};
    math::Vec2f resolution{1.0f, 1.0f};
};

// Weapon effect parameters (laser shots, projectile impacts, etc.)
struct WeaponParams
{
    math::Vec3f position{0.0f, 0.0f, 0.0f};
    math::Vec3f direction{0.0f, 0.0f, -1.0f};
    float intensity = 1.0f;
    float size = 1.0f;
    math::Vec4f color{0.8f, 0.9f, 1.0f, 1.0f}; // Blue/White default
    float time = 0.0f;
};

// Weapon effect system (rendering pending).
class WeaponEffect
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

    [[nodiscard]] static std::expected<WeaponEffect, std::string> create(const CreateInfo& info);

    ~WeaponEffect();

    WeaponEffect(const WeaponEffect&) = delete;
    WeaponEffect& operator=(const WeaponEffect&) = delete;

    WeaponEffect(WeaponEffect&& other) noexcept;
    WeaponEffect& operator=(WeaponEffect&& other) noexcept;

    // Render a weapon effect
    void render(VkCommandBuffer cmd, const WeaponRenderContext& context, const WeaponParams& params);

private:
    WeaponEffect() = default;
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    std::optional<internal::BillboardEffect> effect_;
};

} // namespace starforge::environment
