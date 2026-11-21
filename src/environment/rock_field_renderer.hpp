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

struct RockRenderContext
{
    math::Mat4f viewProj{};
    math::Vec2f projectionScale{1.0f, 1.0f}; // proj[0][0], proj[1][1]
    math::Vec2f resolution{1.0f, 1.0f};
    math::Vec3f sunDirection{1.0f, 0.5f, 0.3f};
};

struct RockDrawParams
{
    std::uint64_t vertexAddress = 0;
    std::uint64_t nearInstanceAddress = 0;
    std::uint32_t nearInstanceCount = 0;
    std::uint64_t farInstanceAddress = 0;
    std::uint32_t farInstanceCount = 0;
    std::uint32_t indexCount = 0;
    bool outline = true;
};

// M15.0.8/15.0.9 instanced rock field: a shared lumpy base mesh drawn once
// over per-instance storage (varied tint + procedural shape), a distance-aware
// inverted-hull outline, and a cheap impostor draw for distant rocks.
class RockFieldRenderer
{
public:
    struct CreateInfo
    {
        VkDevice device = VK_NULL_HANDLE;
        VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
        std::string shaderDirectory;
        // Specialization constants for rock_field.slang. HDR offscreen targets
        // pass vulkan::hdrOutputSpecialization(true) to keep linear values.
        const VkSpecializationInfo* specializationInfo = nullptr;
    };

    static std::expected<RockFieldRenderer, std::string> create(const CreateInfo& info);
    ~RockFieldRenderer();
    RockFieldRenderer(RockFieldRenderer&&) noexcept;
    RockFieldRenderer& operator=(RockFieldRenderer&&) noexcept;
    RockFieldRenderer(const RockFieldRenderer&) = delete;
    RockFieldRenderer& operator=(const RockFieldRenderer&) = delete;

    // Caller binds the base-mesh index buffer before this call.
    void render(VkCommandBuffer cmd, const RockRenderContext& context, const RockDrawParams& params) const;

private:
    RockFieldRenderer() = default;
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    std::optional<vulkan::GraphicsPipeline> mesh_;
    std::optional<vulkan::GraphicsPipeline> outline_;
    std::optional<vulkan::GraphicsPipeline> impostor_;
};

} // namespace starforge::environment
