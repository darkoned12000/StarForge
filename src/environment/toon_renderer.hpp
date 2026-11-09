#pragma once

#include "math/math.hpp"
#include "vulkan/vulkan_pipeline.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>

namespace starforge::gpu_resources
{
class Allocator;
class Buffer;
} // namespace starforge::gpu_resources

namespace starforge::environment
{

struct ToonRenderContext
{
    // Clip matrix (projection * modelView) and the modelView matrix
    // (view space: camera at origin looking down -Z, matching the toon
    // shader's view-space lighting).
    math::Mat4f viewProj{};
    math::Mat4f modelView{1.0f};
    // Direction toward the light, in view space.
    math::Vec3f lightDirectionView{0.0f, 0.0f, 1.0f};
    // Target resolution in pixels; used to keep outline thickness
    // constant in screen space. Zero disables pixel scaling (raw width).
    math::Vec2f resolution{0.0f, 0.0f};
};

struct ToonParams
{
    math::Vec4f baseColor{1.0f, 0.25f, 0.2f, 1.0f};
    math::Vec4f shadowColor{0.25f, 0.05f, 0.1f, 1.0f};
    // Optional middle color of the diffuse ramp. Alpha == 0 means the
    // midpoint of shadowColor and baseColor; set alpha to 1 when using a
    // custom middle tint.
    math::Vec4f midColor{0.0f, 0.0f, 0.0f, 0.0f};
    float bandCount = 3.0f;
    float rimStrength = 0.5f;
    // Outline thickness in screen pixels (converted to view units per
    // draw from the context resolution and model distance).
    float outlineWidth = 2.0f;
};

// Cel-shaded (toon) renderer: quantized diffuse bands, stepped specular,
// rim light, and an inverted-hull outline pass, over a static cube until
// real mesh assets land (M17). Lifetime matches StarfieldRenderer: the
// caller must ensure the GPU is idle before destroying or recreating
// while recorded frames may be in flight.
class ToonRenderer
{
public:
    struct CreateInfo
    {
        VkInstance instance = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        std::uint32_t queueFamily = 0;
        std::uint32_t framesInFlight = 2;
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
        std::string shaderDirectory{};
    };

    [[nodiscard]] static std::expected<ToonRenderer, std::string> create(const CreateInfo& info);

    ~ToonRenderer();

    ToonRenderer(const ToonRenderer&) = delete;
    ToonRenderer& operator=(const ToonRenderer&) = delete;

    ToonRenderer(ToonRenderer&& other) noexcept;
    ToonRenderer& operator=(ToonRenderer&& other) noexcept;

    // Records the outline pass followed by the toon-shaded mesh into an
    // active dynamic-rendering pass. The caller must have set viewport
    // and scissor for the target extent.
    void render(VkCommandBuffer cmd, const ToonRenderContext& context, const ToonParams& params);

private:
    ToonRenderer() = default;
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    std::uint32_t framesInFlight_ = 2;
    std::uint32_t currentDrawSlot_ = 0;
    std::unique_ptr<gpu_resources::Allocator> allocator_;
    std::unique_ptr<gpu_resources::Buffer> drawDataBuffer_;
    std::optional<vulkan::GraphicsPipeline> toonPipeline_;
    std::optional<vulkan::GraphicsPipeline> outlinePipeline_;
};

} // namespace starforge::environment
