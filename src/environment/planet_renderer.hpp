#pragma once

#include "math/math.hpp"
#include "vulkan/vulkan_pipeline.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <expected>
#include <optional>
#include <string>

namespace starforge::environment
{

struct PlanetRenderContext
{
    math::Mat4f viewProj{};
    math::Mat4f invViewProj{};
    math::Vec3f cameraPosition{}; // camera-relative space of viewProj
    math::Vec2f resolution{};
    float time = 0.0f;
};

struct PlanetParams
{
    math::Vec3f center{}; // camera-relative planet centre
    float radius = 6000.0f;
    math::Vec4f baseColor{0.15f, 0.45f, 1.0f, 1.0f};
    math::Vec4f shadowColor{0.02f, 0.12f, 0.35f, 1.0f};
    math::Vec4f rimColor{0.6f, 0.8f, 1.0f, 1.0f};
    math::Vec3f sunDirection{0.4f, 0.5f, 0.75f};
    float fresnelPower = 3.0f;
    float rimStrength = 0.8f;
    float atmosphereRatio = 1.15f;
    float atmosphereFalloff = 2.0f;
    math::Vec4f atmosphereColor{0.3f, 0.55f, 1.0f, 1.0f};
    float atmosphereIntensity = 1.0f;
    float depthBias = 0.0005f;
    float spin = 0.0f;
};

// M15.0.4 planet ray-sphere billboard + atmosphere limb.
// A fullscreen triangle ray-casts the opaque planet disc (writing projective
// depth so nearer objects occlude it) and adds a Fresnel-weighted atmosphere
// limb. All coordinates are camera-relative floats.
class PlanetRenderer
{
public:
    struct CreateInfo
    {
        VkDevice device = VK_NULL_HANDLE;
        VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
        std::string shaderDirectory;
        // Specialization constants for planet_sphere.slang. HDR offscreen
        // targets pass vulkan::hdrOutputSpecialization(true) to keep linear
        // values.
        const VkSpecializationInfo* specializationInfo = nullptr;
    };

    static std::expected<PlanetRenderer, std::string> create(const CreateInfo& info);
    ~PlanetRenderer();
    PlanetRenderer(PlanetRenderer&&) noexcept;
    PlanetRenderer& operator=(PlanetRenderer&&) noexcept;
    PlanetRenderer(const PlanetRenderer&) = delete;
    PlanetRenderer& operator=(const PlanetRenderer&) = delete;

    void render(VkCommandBuffer cmd, const PlanetRenderContext& context, const PlanetParams& params) const;

private:
    PlanetRenderer() = default;
    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkShaderModule sphereVert_ = VK_NULL_HANDLE;
    VkShaderModule sphereFrag_ = VK_NULL_HANDLE;
    VkShaderModule atmoVert_ = VK_NULL_HANDLE;
    VkShaderModule atmoFrag_ = VK_NULL_HANDLE;
    VkPipelineLayout sphereLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout atmoLayout_ = VK_NULL_HANDLE;
    std::optional<vulkan::GraphicsPipeline> spherePipeline_;
    std::optional<vulkan::GraphicsPipeline> atmoPipeline_;
};

} // namespace starforge::environment
