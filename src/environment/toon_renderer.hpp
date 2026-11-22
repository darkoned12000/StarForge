#pragma once

#include "math/math.hpp"
#include "vulkan/vulkan_pipeline.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
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

    // Composable material capabilities (bitmask, decoded in toon.slang).
    // 1 = emissive, 2 = fresnel shield, 4 = faceted, 8 = scattering rim,
    // 16 = shadows/attenuation/detail.
    std::uint32_t capabilities = 0;
    math::Vec4f emissiveColor{0.0f, 0.0f, 0.0f, 0.0f}; // rgb + intensity
    math::Vec4f fresnelColor{0.0f, 0.0f, 0.0f, 0.0f};  // rgb + strength
    math::Vec4f materialParams{0.0f, 0.0f, 0.0f, 0.0f}; // facet, shadow, attenuation, detail
    math::Vec4f highlightColor{0.0f, 0.0f, 0.0f, 0.0f}; // fourth ramp stop + threshold
};

// One mesh vertex: position, shading normal, baked smooth outline normal.
struct ToonVertex
{
    math::Vec3f position{};
    math::Vec3f normal{};
    math::Vec3f outlineNormal{};
};

// Extended vertex for glTF-style textured materials (five float4 per vertex).
struct ToonTexturedVertex
{
    math::Vec3f position{};
    math::Vec3f normal{};
    math::Vec3f outlineNormal{};
    math::Vec2f uv{};
    math::Vec4f tangent{1.0f, 0.0f, 0.0f, 1.0f};
};

// GPU-owned toon mesh (BDA vertex array, three float4 per vertex). Move-only.
class ToonMesh
{
public:
    ToonMesh() = default;
    ~ToonMesh();
    ToonMesh(ToonMesh&&) noexcept;
    ToonMesh& operator=(ToonMesh&&) noexcept;
    ToonMesh(const ToonMesh&) = delete;
    ToonMesh& operator=(const ToonMesh&) = delete;

    [[nodiscard]] std::uint64_t deviceAddress() const noexcept { return address_; }
    [[nodiscard]] std::uint32_t vertexCount() const noexcept { return vertexCount_; }
    [[nodiscard]] bool textured() const noexcept { return textureWidth_ > 0; }
    [[nodiscard]] std::uint64_t textureAddress() const noexcept { return textureAddress_; }
    [[nodiscard]] std::uint32_t textureWidth() const noexcept { return textureWidth_; }
    [[nodiscard]] std::uint32_t textureHeight() const noexcept { return textureHeight_; }

private:
    friend class ToonRenderer;
    ToonMesh(std::shared_ptr<gpu_resources::Allocator> allocator,
             std::unique_ptr<gpu_resources::Buffer> buffer,
             std::uint64_t address, std::uint32_t count) noexcept;

    // Shared so a mesh stays valid if its ToonRenderer is recreated.
    std::shared_ptr<gpu_resources::Allocator> allocator_;
    std::unique_ptr<gpu_resources::Buffer> buffer_;
    std::unique_ptr<gpu_resources::Buffer> textureBuffer_;
    std::uint64_t address_ = 0;
    std::uint64_t textureAddress_ = 0;
    std::uint32_t vertexCount_ = 0;
    std::uint32_t textureWidth_ = 0;
    std::uint32_t textureHeight_ = 0;
};

// Cel-shaded (toon) renderer: quantized diffuse bands, stepped specular,
// rim light, inverted-hull outlines, and composable material capabilities.
// Geometry may be the built-in cube or a caller-created ToonMesh.
// Lifetime matches StarfieldRenderer: the caller must ensure the GPU is
// idle before destroying or recreating while recorded frames are in flight.
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
        // Specialization constants for the toon shaders. HDR offscreen targets
        // pass vulkan::hdrOutputSpecialization(true) to keep linear values.
        const VkSpecializationInfo* specializationInfo = nullptr;
    };

    [[nodiscard]] static std::expected<ToonRenderer, std::string> create(const CreateInfo& info);

    ~ToonRenderer();

    ToonRenderer(const ToonRenderer&) = delete;
    ToonRenderer& operator=(const ToonRenderer&) = delete;

    ToonRenderer(ToonRenderer&& other) noexcept;
    ToonRenderer& operator=(ToonRenderer&& other) noexcept;

    // Resets per-frame draw bookkeeping. Call once per frame before issuing
    // toon draws when using a fixed frame-in-flight loop.
    void beginFrame() noexcept;

    // Records the outline pass followed by the toon-shaded mesh into an
    // active dynamic-rendering pass. The caller must have set viewport
    // and scissor for the target extent.
    void render(VkCommandBuffer cmd, const ToonRenderContext& context, const ToonParams& params);

    // Uploads a mesh into renderer-owned GPU memory. The renderer must
    // outlive every mesh it creates.
    [[nodiscard]] std::expected<ToonMesh, std::string>
    createMesh(std::span<const ToonVertex> vertices);

    // Textured mesh from glTF-style attributes and RGBA8 base-colour pixels.
    [[nodiscard]] std::expected<ToonMesh, std::string>
    createTexturedMesh(std::span<const ToonTexturedVertex> vertices,
                       const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height);

    // Records one draw with explicit mesh geometry. An invalid mesh falls
    // back to the built-in cube.
    void renderMesh(VkCommandBuffer cmd, const ToonRenderContext& context,
                    const ToonParams& params, const ToonMesh& mesh);

private:
    ToonRenderer() = default;
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    std::uint32_t framesInFlight_ = 2;
    std::uint32_t maxDrawsPerFrame_ = 512;
    std::uint32_t drawsInFrame_ = 0;
    std::uint32_t currentDrawSlot_ = 0;
    std::shared_ptr<gpu_resources::Allocator> allocator_;
    std::unique_ptr<gpu_resources::Buffer> drawDataBuffer_;
    void* mappedDrawData_ = nullptr;
    std::optional<vulkan::GraphicsPipeline> toonPipeline_;
    std::optional<vulkan::GraphicsPipeline> outlinePipeline_;
    std::optional<vulkan::GraphicsPipeline> emissivePipeline_; // additive glow
};

} // namespace starforge::environment
