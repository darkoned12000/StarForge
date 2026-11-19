#pragma once

#include "starforge/math.hpp"
#include "starforge/renderer.hpp"

#include <expected>
#include <memory>
#include <string>

namespace starforge
{

// M15.0.4 visitable planet: a ray-sphere billboard that writes projective
// depth (so it occludes correctly against nearby objects), shaded with toon
// diffuse bands and a Fresnel limb, plus a Fresnel-weighted atmosphere glow.
// All positions are in the camera-relative space of `viewProj` (metres).
// Move-only; the renderer passed to create() must outlive the planet.
class Planet
{
public:
    struct CreateInfo
    {
        std::string shaderDirectory;
    };

    struct RenderContext
    {
        math::Mat4f viewProj{};
        math::Mat4f invViewProj{};
        math::Vec3f cameraPosition{}; // camera-relative position of the eye
        math::Vec2f resolution{};
        float time = 0.0f;
    };

    struct Params
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
        float spin = 0.0f; // axial rotation in radians (procedural surface detail)
    };

    [[nodiscard]] static std::expected<Planet, std::string> create(Renderer& renderer,
                                                                   const CreateInfo& info);
    ~Planet();
    Planet(Planet&&) noexcept;
    Planet& operator=(Planet&&) noexcept;
    Planet(const Planet&) = delete;
    Planet& operator=(const Planet&) = delete;

    void record(FrameContext& frameContext, const RenderContext& context, const Params& params);
    [[nodiscard]] std::expected<Renderer::FrameResult, std::string>
    render(Renderer& renderer, const RenderContext& context, const Params& params);
    [[nodiscard]] std::expected<void, std::string> recreate(Renderer& renderer);

private:
    struct Impl;
    explicit Planet(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace starforge
