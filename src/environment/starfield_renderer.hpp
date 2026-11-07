#pragma once

#include "internal/effect_pipeline.hpp"
#include "math/math.hpp"
#include "vulkan/vulkan_pipeline.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace starforge::environment
{

struct StarfieldRenderContext
{
    math::Mat4f viewProj{};
    math::Mat4f invViewProj{1.0f}; // Unproject for world-locked directions.
    math::Vec3f cameraPosition{0.0f, 0.0f, 0.0f};
    // Target resolution in pixels (used for resolution-independent star density).
    math::Vec2f resolution{1.0f, 1.0f};
    float time = 0.0f;
};

struct StarfieldParams
{
    math::Vec3f worldPosition{0.0f, 0.0f, 0.0f};
    float radius = 1.0f;
    float intensity = 1.0f;
    math::Vec4f color{1.0f, 0.9f, 0.7f, 1.0f}; // Yellow/Orange default
    float limbDarkeningCoeff = 0.3f;
    float time = 0.0f;
};

// Star rendering system for suns and stars in space.
// Currently renders a procedural starfield (fullscreen triangle, push
// constants only, no vertex buffers) using starfield.vert.spv / starfield.frag.spv
// loaded from `shaderDirectory`.
//
// Lifetime: the caller must ensure the GPU is idle (e.g. vkDeviceWaitIdle)
// before destroying, moving, or recreating a StarfieldRenderer while command
// buffers recorded with its pipeline may still be in flight.
class StarfieldRenderer
{
public:
    struct CreateInfo
    {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        std::uint32_t queueFamily = 0;
        // Color attachment format the pipeline is created for
        // (e.g. swapchain format or offscreen image format).
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        // Depth attachment format (VK_FORMAT_UNDEFINED = no depth).
        // Unused by the starfield pipeline today; kept so all effect
        // CreateInfos stay uniform.
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
        // Directory containing starfield.vert.spv and starfield.frag.spv.
        std::string shaderDirectory{};
    };

    [[nodiscard]] static std::expected<StarfieldRenderer, std::string> create(const CreateInfo& info);

    ~StarfieldRenderer();

    // Explicitly delete copy semantics
    StarfieldRenderer(const StarfieldRenderer&) = delete;
    StarfieldRenderer& operator=(const StarfieldRenderer&) = delete;

    // Explicitly allow move semantics
    StarfieldRenderer(StarfieldRenderer&& other) noexcept;
    StarfieldRenderer& operator=(StarfieldRenderer&& other) noexcept;

    // Record the starfield into an active dynamic-rendering pass.
    // The caller must have set viewport and scissor for the target extent;
    // `context`/`params` supply time and intensity via push constants.
    void render(VkCommandBuffer cmd, const StarfieldRenderContext& context, const StarfieldParams& params);

private:
    StarfieldRenderer() = default;
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    std::optional<vulkan::GraphicsPipeline> pipeline_;
    // Sun billboard (billboard.slang), drawn at StarfieldParams.worldPosition.
    std::optional<internal::BillboardEffect> sunEffect_;
};

} // namespace starforge::environment
