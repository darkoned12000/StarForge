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

// M17.5 instanced combat effects. All transient effects (bolts, sparks,
// shockwaves, fireballs, engine plumes) share one draw and one fixed-
// capacity instance buffer. The caller owns the buffer and its lifetime.
struct CombatFxRenderContext
{
    math::Mat4f viewProj{};
    math::Vec4f cameraRight{};
    math::Vec4f cameraUp{};
    math::Vec2f resolution{1.0f, 1.0f};
    // Camera position in camera-relative world space; needed to build a
    // safe billboard side vector for bolts passing through the origin.
    math::Vec3f cameraPosition{};
};

struct CombatFxDrawParams
{
    std::uint64_t instanceAddress = 0;
    std::uint32_t instanceCount = 0;
};

class CombatFxRenderer
{
public:
    struct CreateInfo
    {
        VkDevice device = VK_NULL_HANDLE;
        VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
        std::string shaderDirectory;
        const VkSpecializationInfo* specializationInfo = nullptr;
    };

    [[nodiscard]] static std::expected<CombatFxRenderer, std::string> create(const CreateInfo& info);
    ~CombatFxRenderer();
    CombatFxRenderer(CombatFxRenderer&&) noexcept;
    CombatFxRenderer& operator=(CombatFxRenderer&&) noexcept;
    CombatFxRenderer(const CombatFxRenderer&) = delete;
    CombatFxRenderer& operator=(const CombatFxRenderer&) = delete;

    void render(VkCommandBuffer cmd, const CombatFxRenderContext& context,
                const CombatFxDrawParams& params) const;

private:
    CombatFxRenderer() = default;
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    std::optional<vulkan::GraphicsPipeline> pipeline_;
};

} // namespace starforge::environment
