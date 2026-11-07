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

struct StormRenderContext
{
    math::Mat4f viewProj{};
    math::Mat4f invViewProj{1.0f};
    math::Vec3f cameraPosition{0.0f, 0.0f, 0.0f};
    math::Vec2f resolution{1.0f, 1.0f};
};

struct StormParams
{
    math::Vec4f color{0.45f, 0.5f, 0.55f, 1.0f}; // Grey cloud layer
    float intensity = 1.0f;
    float wind = 1.0f;
    float time = 0.0f;
};

// Storm cloud layer effect (animated FBM cloud pass).
class StormEffect
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

    [[nodiscard]] static std::expected<StormEffect, std::string> create(const CreateInfo& info);

    ~StormEffect();

    StormEffect(const StormEffect&) = delete;
    StormEffect& operator=(const StormEffect&) = delete;

    StormEffect(StormEffect&& other) noexcept;
    StormEffect& operator=(StormEffect&& other) noexcept;

    void render(VkCommandBuffer cmd, const StormRenderContext& context, const StormParams& params);

private:
    StormEffect() = default;
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    std::optional<internal::FullscreenEffect> effect_;
};

} // namespace starforge::environment
