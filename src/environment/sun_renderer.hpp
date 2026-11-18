#pragma once

#include "math/math.hpp"
#include "planets/cubed_sphere.hpp"
#include "planets/planet_patch.hpp"
#include "spatial/world_position.hpp"
#include "vulkan/vulkan_pipeline.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace starforge::gpu_resources
{
class Allocator;
class Buffer;
} // namespace starforge::gpu_resources

namespace starforge::environment
{

struct SunRenderContext
{
    math::Mat4f viewProj{};                 // Camera projection * view
    spatial::WorldPosition cameraPosition{}; // 64-bit camera position
    math::Vec2f resolution{0.0f, 0.0f};      // Viewport extent
    float time = 0.0f;
};

struct SunParams
{
    spatial::WorldPosition worldPosition{};           // Sun celestial position
    double radius = 10000.0;                          // Photosphere radius in meters
    double coronaRadiusMultiplier = 6.0;             // Outer corona radius (5x - 10x)
    math::Vec4f coreColor{1.0f, 0.95f, 0.82f, 1.0f};  // Bright disk emission
    math::Vec4f limbColor{0.95f, 0.38f, 0.05f, 1.0f}; // Limb darkening rim color
    math::Vec4f coronaColor{1.0f, 0.65f, 0.20f, 1.0f};// Corona atmospheric glow
    float limbDarkeningCoeff = 0.6f;                  // Eddington factor u in 1 - u(1 - μ)
    float coronaIntensity = 1.0f;
    float time = 0.0f;
};

// SunRenderer (M15.0.7): Renders a visitable 3D celestial sun with:
// - Opaque emissive core with Eddington limb darkening (1 - u(1 - μ))
// - M7 quadtree patch LOD with LargeObjectSetup for zero float32 jitter at astronomical scale
// - Additive corona sphere at 5-10x radius, occluded behind the opaque disc
class SunRenderer
{
public:
    struct CreateInfo
    {
        VkInstance instance = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
        std::string shaderDirectory{};
        double defaultRadius = 10000.0;
        std::uint32_t subdivisions = 16;
    };

    [[nodiscard]] static std::expected<SunRenderer, std::string> create(const CreateInfo& info);

    ~SunRenderer();

    SunRenderer(const SunRenderer&) = delete;
    SunRenderer& operator=(const SunRenderer&) = delete;

    SunRenderer(SunRenderer&& other) noexcept;
    SunRenderer& operator=(SunRenderer&& other) noexcept;

    // Renders the opaque sun core followed by the additive corona into an
    // active dynamic rendering pass.
    void render(VkCommandBuffer cmd, const SunRenderContext& context, const SunParams& params);

    [[nodiscard]] double currentRadius() const noexcept { return currentRadius_; }

private:
    SunRenderer() = default;
    void destroy() noexcept;

    [[nodiscard]] std::expected<void, std::string> buildGeometry(double radius, std::uint32_t subdivisions);

    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineLayout corePipelineLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout coronaPipelineLayout_ = VK_NULL_HANDLE;
    std::optional<vulkan::GraphicsPipeline> corePipeline_;
    std::optional<vulkan::GraphicsPipeline> coronaPipeline_;

    std::unique_ptr<gpu_resources::Allocator> allocator_;
    std::unique_ptr<gpu_resources::Buffer> coreVertexBuffer_;
    std::unique_ptr<gpu_resources::Buffer> coreIndexBuffer_;
    std::unique_ptr<gpu_resources::Buffer> coronaVertexBuffer_;
    std::unique_ptr<gpu_resources::Buffer> coronaIndexBuffer_;

    double currentRadius_ = 0.0;
    std::uint32_t subdivisions_ = 16;
    std::uint32_t patchVertexCount_ = 0;
    std::uint32_t patchIndexCount_ = 0;
    std::vector<planets::PlanetPatch> facePatches_;
};

} // namespace starforge::environment
