#pragma once

// Shared Vulkan pipeline helpers for environment effects (internal).
// FullscreenEffect: fullscreen-triangle passes (fragment push constants).
// BillboardEffect: NDC-centered quad passes (fragment + vertex push).

#include "math/math.hpp"
#include "vulkan/vulkan_pipeline.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <type_traits>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace starforge::environment::internal
{

enum class BillboardShape : std::uint32_t
{
    Disc = 0,
    Beam = 1,
    Ring = 2,
    Cone = 3,
};

// Must match PushConstants in billboard.slang (48 bytes).
struct BillboardPush
{
    math::Vec4f centerSize{0.0f, 0.0f, 0.1f, 1.0f}; // xy: NDC center, z: half-size, w: aspect
    math::Vec4f color{1.0f, 1.0f, 1.0f, 1.0f};
    float time = 0.0f;
    float intensity = 1.0f;
    std::uint32_t shape = 0;
    float rotationAngle = 0.0f; // Screen-space rotation in radians (beam/cone orientation)
};

static_assert(std::is_standard_layout_v<BillboardPush>, "BillboardPush must be standard layout");

inline math::Vec3f projectToNdc(const math::Mat4f& viewProj, const math::Vec3f& worldPos)
{
    const math::Vec4f clip = viewProj * math::Vec4f{worldPos, 1.0f};
    if (clip.w <= 1e-4f)
    {
        return math::Vec3f{2.0f, 2.0f, 2.0f}; // Culled behind near plane
    }
    return math::Vec3f{clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
}

inline float ndcHalfSize(const math::Mat4f& viewProj, const math::Vec3f& worldPos, float worldRadius)
{
    const math::Vec4f clip = viewProj * math::Vec4f{worldPos, 1.0f};
    if (clip.w <= 1e-4f)
    {
        return 1e-4f;
    }
    // Perspective scaling: P00 = cot(fovX / 2), size = r * P00 / w.
    // Robust for any camera orientation (no axis sampling).
    const float projScale = std::abs(viewProj[0][0]);
    return std::max(1e-4f, (worldRadius * projScale) / clip.w);
}

// Screen-space angle of a world direction through the view transform.
inline float projectedAngle(const math::Mat4f& viewProj, const math::Vec3f& origin, const math::Vec3f& direction)
{
    const math::Vec4f c0 = viewProj * math::Vec4f{origin, 1.0f};
    const math::Vec4f c1 = viewProj * math::Vec4f{origin + direction, 1.0f};
    if (std::abs(c0.w) < 1e-6f || std::abs(c1.w) < 1e-6f)
    {
        return 0.0f;
    }
    const float dx = (c1.x / c1.w) - (c0.x / c0.w);
    const float dy = (c1.y / c1.w) - (c0.y / c0.w);
    return std::atan2(dy, dx);
}

class FullscreenEffect
{
public:
    struct CreateInfo
    {
        VkDevice device = VK_NULL_HANDLE;
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        std::string shaderDirectory;
        std::string shaderBaseName; // e.g. "nebula" -> nebula.vert.spv/nebula.frag.spv
        std::uint32_t pushConstantSize = 0;
    };

    [[nodiscard]] static std::expected<FullscreenEffect, std::string> create(const CreateInfo& info);

    FullscreenEffect() = default;
    ~FullscreenEffect();

    FullscreenEffect(const FullscreenEffect&) = delete;
    FullscreenEffect& operator=(const FullscreenEffect&) = delete;
    FullscreenEffect(FullscreenEffect&& other) noexcept;
    FullscreenEffect& operator=(FullscreenEffect&& other) noexcept;

    void render(VkCommandBuffer cmd, const void* pushData, std::uint32_t pushSize) const;

private:
    void destroy() noexcept;
    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    std::optional<vulkan::GraphicsPipeline> pipeline_;
};

class BillboardEffect
{
public:
    struct CreateInfo
    {
        VkDevice device = VK_NULL_HANDLE;
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        std::string shaderDirectory;
        std::string shaderBaseName; // e.g. "billboard"
        std::uint32_t pushConstantSize = 0;
    };

    [[nodiscard]] static std::expected<BillboardEffect, std::string> create(const CreateInfo& info);

    BillboardEffect() = default;
    ~BillboardEffect();

    BillboardEffect(const BillboardEffect&) = delete;
    BillboardEffect& operator=(const BillboardEffect&) = delete;
    BillboardEffect(BillboardEffect&& other) noexcept;
    BillboardEffect& operator=(BillboardEffect&& other) noexcept;

    void render(VkCommandBuffer cmd, const void* pushData, std::uint32_t pushSize) const;

private:
    void destroy() noexcept;
    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    std::optional<vulkan::GraphicsPipeline> pipeline_;
};

} // namespace starforge::environment::internal
