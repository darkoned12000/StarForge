#pragma once

#include "math/math.hpp"
#include "vulkan/vulkan_pipeline.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace starforge::environment
{

// CPU-side displaced-rock mesh (interleaved float4 position / float4
// normal pairs, 32 bytes/vertex, indexed).
struct AsteroidMesh
{
    std::vector<float> vertices; // [pos xyzw, normal xyzw] pairs per vertex
    std::vector<std::uint32_t> indices;
};

// Displaced UV-sphere rock: hash-noise radius displacement per vertex.
// Deterministic (seeded); flat-ish normals from displacement gradient.
[[nodiscard]] AsteroidMesh generateAsteroidMesh(float radius, std::uint32_t latSegments, std::uint32_t lonSegments);

struct AsteroidFieldRenderContext
{
    math::Mat4f viewProj{};
    math::Vec3f cameraPosition{0.0f, 0.0f, 0.0f};
    math::Vec2f resolution{1.0f, 1.0f};
};

// Per-instance transform: xyz position, w scale + unit quaternion
// orientation (multi-axis tumble). Matches InstanceData in asteroid.slang.
struct AsteroidInstance
{
    math::Vec4f posScale{0.0f, 0.0f, 0.0f, 1.0f};
    math::Vec4f rotation{0.0f, 0.0f, 0.0f, 1.0f};
};

struct AsteroidFieldParams
{
    // BDA addresses of buffers owned by the caller (kept alive during use).
    std::uint64_t vertexBufferAddress = 0;   // float4 positions + float4 normals interleaved (32 B)
    std::uint64_t instanceBufferAddress = 0; // AsteroidInstance array (32 B stride)
    std::uint32_t indexCount = 0;
    std::uint32_t instanceCount = 0;
    float time = 0.0f;
};

// Instanced asteroid field renderer (displaced rock, lambert, depth).
class AsteroidFieldRenderer
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

    [[nodiscard]] static std::expected<AsteroidFieldRenderer, std::string> create(const CreateInfo& info);

    ~AsteroidFieldRenderer();

    AsteroidFieldRenderer(const AsteroidFieldRenderer&) = delete;
    AsteroidFieldRenderer& operator=(const AsteroidFieldRenderer&) = delete;

    AsteroidFieldRenderer(AsteroidFieldRenderer&& other) noexcept;
    AsteroidFieldRenderer& operator=(AsteroidFieldRenderer&& other) noexcept;

    void render(VkCommandBuffer cmd, const AsteroidFieldRenderContext& context, const AsteroidFieldParams& params);

private:
    AsteroidFieldRenderer() = default;
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    std::optional<vulkan::GraphicsPipeline> pipeline_;
};

} // namespace starforge::environment
